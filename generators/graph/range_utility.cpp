#include "range_utility.h"
#include "../../util/noise/fast_noise_lite.h"

#include <core/image.h>
#include <modules/opensimplex/open_simplex_noise.h>
#include <scene/resources/curve.h>

Interval get_osn_octave_range_2d(OpenSimplexNoise *noise, const Interval &x, const Interval &y, int octave) {
	// Any unit vector away from a given evaluation point, the maximum difference is a fixed number.
	// We can use that number to find a bounding range within our rectangular interval.
	static const float max_derivative = 2.35;
	static const float max_derivative_half_diagonal = 0.5f * max_derivative * Math_SQRT2;

	float mid_x = 0.5 * (x.min + x.max);
	float mid_y = 0.5 * (y.min + y.max);
	float mid_value = noise->_get_octave_noise_2d(octave, mid_x, mid_y);

	float diag = Math::sqrt(squared(x.length()) + squared(y.length()));

	return Interval(
			::max(mid_value - max_derivative_half_diagonal * diag, -1.f),
			::min(mid_value + max_derivative_half_diagonal * diag, 1.f));
}

Interval get_osn_octave_range_3d(
		OpenSimplexNoise *noise, const Interval &x, const Interval &y, const Interval &z, int octave) {
	// Any unit vector away from a given evaluation point, the maximum difference is a fixed number.
	// We can use that number to find a bounding range within our box interval.
	static const float max_derivative = 2.5;
	static const float max_derivative_half_diagonal = 0.5f * max_derivative * Math_SQRT2;

	float mid_x = 0.5 * (x.min + x.max);
	float mid_y = 0.5 * (y.min + y.max);
	float mid_z = 0.5 * (z.min + z.max);
	float mid_value = noise->_get_octave_noise_3d(octave, mid_x, mid_y, mid_z);

	float diag = Math::sqrt(squared(x.length()) + squared(y.length()) + squared(z.length()));

	return Interval(
			::max(mid_value - max_derivative_half_diagonal * diag, -1.f),
			::min(mid_value + max_derivative_half_diagonal * diag, 1.f));
}

Interval get_osn_range_2d(OpenSimplexNoise *noise, Interval x, Interval y) {
	// Same implementation as `get_noise_2d`

	if (x.is_single_value() && y.is_single_value()) {
		return Interval::from_single_value(noise->get_noise_2d(x.min, y.min));
	}

	x /= noise->get_period();
	y /= noise->get_period();

	float amp = 1.0;
	float max = 1.0;
	Interval sum = get_osn_octave_range_2d(noise, x, y, 0);

	int i = 0;
	while (++i < noise->get_octaves()) {
		x *= noise->get_lacunarity();
		y *= noise->get_lacunarity();
		amp *= noise->get_persistence();
		max += amp;
		sum += get_osn_octave_range_2d(noise, x, y, i) * amp;
	}

	return sum / max;
}

Interval get_osn_range_3d(OpenSimplexNoise *noise, Interval x, Interval y, Interval z) {
	// Same implementation as `get_noise_3d`

	if (x.is_single_value() && y.is_single_value()) {
		return Interval::from_single_value(noise->get_noise_2d(x.min, y.min));
	}

	x /= noise->get_period();
	y /= noise->get_period();
	z /= noise->get_period();

	float amp = 1.0;
	float max = 1.0;
	Interval sum = get_osn_octave_range_3d(noise, x, y, z, 0);

	int i = 0;
	while (++i < noise->get_octaves()) {
		x *= noise->get_lacunarity();
		y *= noise->get_lacunarity();
		z *= noise->get_lacunarity();
		amp *= noise->get_persistence();
		max += amp;
		sum += get_osn_octave_range_3d(noise, x, y, z, i) * amp;
	}

	return sum / max;
}

Interval get_curve_range(Curve &curve, bool &is_monotonic_increasing) {
	// TODO Would be nice to have the cache directly
	// TODO Also detect if monotonic decreasing, or litterally find peaks
	const int res = curve.get_bake_resolution();
	Interval range;
	float prev_v = curve.interpolate_baked(0.f);
	if (curve.interpolate_baked(1.f) > prev_v) {
		is_monotonic_increasing = true;
	}
	for (int i = 0; i < res; ++i) {
		const float v = curve.interpolate_baked(static_cast<float>(i) / res);
		range.add_point(v);
		if (v < prev_v) {
			is_monotonic_increasing = false;
		}
		prev_v = v;
	}
	return range;
}

Interval get_heightmap_range(Image &im) {
	return get_heightmap_range(im, Rect2i(0, 0, im.get_width(), im.get_height()));
}

Interval get_heightmap_range(Image &im, Rect2i rect) {
	switch (im.get_format()) {
		case Image::FORMAT_R8:
		case Image::FORMAT_RG8:
		case Image::FORMAT_RGB8:
		case Image::FORMAT_RGBA8:
		case Image::FORMAT_RH:
		case Image::FORMAT_RGH:
		case Image::FORMAT_RGBH:
		case Image::FORMAT_RGBAH:
		case Image::FORMAT_RF:
		case Image::FORMAT_RGF:
		case Image::FORMAT_RGBF:
		case Image::FORMAT_RGBAF: {
			Interval r;

			im.lock();

			r.min = im.get_pixel(0, 0).r;
			r.max = r.min;

			const int max_x = rect.position.x + rect.size.x;
			const int max_y = rect.position.y + rect.size.y;

			for (int y = rect.position.y; y < max_y; ++y) {
				for (int x = rect.position.x; x < max_x; ++x) {
					r.add_point(im.get_pixel(x, y).r);
				}
			}

			im.unlock();

			return r;
		} break;

		default:
			ERR_FAIL_V_MSG(Interval(), "Image format not supported");
			break;
	}
	return Interval();
}

static Interval get_fnl_cellular_value_range_2d(const FastNoiseLite *noise, Interval x, Interval y) {
	const float c0 = noise->get_noise_2d(x.min, y.min);
	const float c1 = noise->get_noise_2d(x.max, y.min);
	const float c2 = noise->get_noise_2d(x.min, y.max);
	const float c3 = noise->get_noise_2d(x.max, y.max);
	if (c0 == c1 && c1 == c2 && c2 == c3) {
		return Interval::from_single_value(c0);
	}
	return Interval{ -1, 1 };
}

static Interval get_fnl_cellular_value_range_3d(const FastNoiseLite *noise, Interval x, Interval y, Interval z) {
	const float c0 = noise->get_noise_3d(x.min, y.min, z.min);
	const float c1 = noise->get_noise_3d(x.max, y.min, z.min);
	const float c2 = noise->get_noise_3d(x.min, y.max, z.min);
	const float c3 = noise->get_noise_3d(x.max, y.max, z.min);
	const float c4 = noise->get_noise_3d(x.max, y.max, z.max);
	const float c5 = noise->get_noise_3d(x.max, y.max, z.max);
	const float c6 = noise->get_noise_3d(x.max, y.max, z.max);
	const float c7 = noise->get_noise_3d(x.max, y.max, z.max);
	if (c0 == c1 && c1 == c2 && c2 == c3 && c3 == c4 && c4 == c5 && c5 == c6 && c6 == c7) {
		return Interval::from_single_value(c0);
	}
	return Interval{ -1, 1 };
}

static Interval get_fnl_cellular_range(const FastNoiseLite *noise) {
	// There are many combinations with Cellular noise so instead of implementing them with intervals,
	// I used empiric tests to figure out some bounds.

	// Value mode must be handled separately.

	switch (noise->get_cellular_distance_function()) {
		case FastNoiseLite::CELLULAR_DISTANCE_EUCLIDEAN:
			switch (noise->get_cellular_return_type()) {
				case FastNoiseLite::CELLULAR_RETURN_DISTANCE:
					return Interval{ -1.f, 0.08f };
				case FastNoiseLite::CELLULAR_RETURN_DISTANCE_2:
					return Interval{ -0.92f, 0.35 };
				case FastNoiseLite::CELLULAR_RETURN_DISTANCE_2_ADD:
					return Interval{ -0.92f, 0.1 };
				case FastNoiseLite::CELLULAR_RETURN_DISTANCE_2_SUB:
					return Interval{ -1, 0.15 };
				case FastNoiseLite::CELLULAR_RETURN_DISTANCE_2_MUL:
					return Interval{ -1, 0 };
				case FastNoiseLite::CELLULAR_RETURN_DISTANCE_2_DIV:
					return Interval{ -1, 0 };
				default:
					ERR_FAIL_V(Interval(-1, 1));
			}
			break;

		case FastNoiseLite::CELLULAR_DISTANCE_EUCLIDEAN_SQ:
			switch (noise->get_cellular_return_type()) {
				case FastNoiseLite::CELLULAR_RETURN_DISTANCE:
					return Interval{ -1, 0.2 };
				case FastNoiseLite::CELLULAR_RETURN_DISTANCE_2:
					return Interval{ -1, 0.8 };
				case FastNoiseLite::CELLULAR_RETURN_DISTANCE_2_ADD:
					return Interval{ -1, 0.2 };
				case FastNoiseLite::CELLULAR_RETURN_DISTANCE_2_SUB:
					return Interval{ -1, 0.7 };
				case FastNoiseLite::CELLULAR_RETURN_DISTANCE_2_MUL:
					return Interval{ -1, 0 };
				case FastNoiseLite::CELLULAR_RETURN_DISTANCE_2_DIV:
					return Interval{ -1, 0 };
				default:
					ERR_FAIL_V(Interval(-1, 1));
			}

		case FastNoiseLite::CELLULAR_DISTANCE_MANHATTAN:
			switch (noise->get_cellular_return_type()) {
				case FastNoiseLite::CELLULAR_RETURN_DISTANCE:
					return Interval{ -1, 0.75 };
				case FastNoiseLite::CELLULAR_RETURN_DISTANCE_2:
					return Interval{ -0.9, 0.8 };
				case FastNoiseLite::CELLULAR_RETURN_DISTANCE_2_ADD:
					return Interval{ -0.8, 0.8 };
				case FastNoiseLite::CELLULAR_RETURN_DISTANCE_2_SUB:
					return Interval{ -1.0, 0.5 };
				case FastNoiseLite::CELLULAR_RETURN_DISTANCE_2_MUL:
					return Interval{ -1.0, 0.7 };
				case FastNoiseLite::CELLULAR_RETURN_DISTANCE_2_DIV:
					return Interval{ -1.0, 0.0 };
				default:
					ERR_FAIL_V(Interval(-1, 1));
			}

		case FastNoiseLite::CELLULAR_DISTANCE_HYBRID:
			switch (noise->get_cellular_return_type()) {
				case FastNoiseLite::CELLULAR_RETURN_DISTANCE:
					return Interval{ -1, 1.75 };
				case FastNoiseLite::CELLULAR_RETURN_DISTANCE_2:
					return Interval{ -0.9, 2.3 };
				case FastNoiseLite::CELLULAR_RETURN_DISTANCE_2_ADD:
					return Interval{ -0.9, 1.9 };
				case FastNoiseLite::CELLULAR_RETURN_DISTANCE_2_SUB:
					return Interval{ -1.0, 1.85 };
				case FastNoiseLite::CELLULAR_RETURN_DISTANCE_2_MUL:
					return Interval{ -1.0, 3.4 };
				case FastNoiseLite::CELLULAR_RETURN_DISTANCE_2_DIV:
					return Interval{ -1.0, 0.0 };
				default:
					ERR_FAIL_V(Interval(-1, 1));
			}
	}
	return Interval{ -1.f, 1.f };
}

Interval get_fnl_range_2d(const FastNoiseLite *noise, Interval x, Interval y) {
	// TODO More precise analysis using derivatives
	switch (noise->get_noise_type()) {
		case FastNoiseLite::TYPE_CELLULAR:
			if (noise->get_cellular_return_type() == FastNoiseLite::CELLULAR_RETURN_CELL_VALUE) {
				return get_fnl_cellular_value_range_2d(noise, x, y);
			}
			return get_fnl_cellular_range(noise);
		default:
			return Interval{ -1.f, 1.f };
	}
}

Interval get_fnl_range_3d(const FastNoiseLite *noise, Interval x, Interval y, Interval z) {
	// TODO More precise analysis using derivatives
	switch (noise->get_noise_type()) {
		case FastNoiseLite::TYPE_CELLULAR:
			if (noise->get_cellular_return_type() == FastNoiseLite::CELLULAR_RETURN_CELL_VALUE) {
				return get_fnl_cellular_value_range_3d(noise, x, y, z);
			}
			return get_fnl_cellular_range(noise);
		default:
			return Interval{ -1.f, 1.f };
	}
}

Interval2 get_fnl_gradient_range_2d(const FastNoiseLiteGradient *noise, Interval x, Interval y) {
	// TODO More precise analysis
	const float amp = Math::abs(noise->get_amplitude());
	return Interval2{
		Interval{ x.min - amp, x.max + amp },
		Interval{ y.min - amp, y.max + amp }
	};
}

Interval3 get_fnl_gradient_range_3d(const FastNoiseLiteGradient *noise, Interval x, Interval y, Interval z) {
	// TODO More precise analysis
	const float amp = Math::abs(noise->get_amplitude());
	return Interval3{
		Interval{ x.min - amp, x.max + amp },
		Interval{ y.min - amp, y.max + amp },
		Interval{ z.min - amp, z.max + amp }
	};
}

#ifdef DEBUG_ENABLED

namespace NoiseTests {

const int ITERATIONS = 1000000;
const int STEP_RESOLUTION_COUNT = 100;
const double STEP_MIN = 0.0001;
const double STEP_MAX = 0.01;

enum Tests {
	TEST_MIN_MAX = 1,
	TEST_DERIVATIVES = 2
};

// Sample a maximum change across the given step.
// The result is not normalized for performance.
template <typename F2, typename FloatT>
FloatT get_derivative(FloatT x, FloatT y, FloatT step, F2 noise_func_2d) {
	FloatT n0, n1, d;
	FloatT max_derivative = 0.0;

	n0 = noise_func_2d(x, y);

	n1 = noise_func_2d(x + step, y);
	d = Math::abs(n1 - n0);
	if (d > max_derivative) {
		max_derivative = d;
	}

	n1 = noise_func_2d(x, y + step);
	d = Math::abs(n1 - n0);
	if (d > max_derivative) {
		max_derivative = d;
	}

	return max_derivative;
}

template <typename F3, typename FloatT>
FloatT get_derivative(FloatT x, FloatT y, FloatT z, FloatT step, F3 noise_func_3d) {
	FloatT n0, n1, d;
	FloatT max_derivative = 0.0;

	n0 = noise_func_3d(x, y, z);

	n1 = noise_func_3d(x + step, y, z);
	d = Math::abs(n1 - n0);
	if (d > max_derivative) {
		max_derivative = d;
	}

	n1 = noise_func_3d(x, y + step, z);
	d = Math::abs(n1 - n0);
	if (d > max_derivative) {
		max_derivative = d;
	}

	n1 = noise_func_3d(x, y, z + step);
	d = Math::abs(n1 - n0);
	if (d > max_derivative) {
		max_derivative = d;
	}

	return max_derivative;
}

template <typename F2, typename F3, typename FloatT>
void test_min_max(F2 noise_func_2d, F3 noise_func_3d) {
	FloatT min_value_2d = std::numeric_limits<FloatT>::max();
	FloatT max_value_2d = std::numeric_limits<FloatT>::min();

	FloatT min_value_3d = std::numeric_limits<FloatT>::max();
	FloatT max_value_3d = std::numeric_limits<FloatT>::min();

	for (int i = 0; i < ITERATIONS; ++i) {
		FloatT x = Math::randd() * 2000.0 - 1000.0;
		FloatT y = Math::randd() * 2000.0 - 1000.0;
		FloatT z = Math::randd() * 2000.0 - 1000.0;

		FloatT n = noise_func_2d(x, y);

		min_value_2d = min(n, min_value_2d);
		max_value_2d = max(n, max_value_2d);

		n = noise_func_3d(x, y, z);

		min_value_3d = min(n, min_value_3d);
		max_value_3d = max(n, max_value_3d);
	}

	print_line(String("2D | Min: {0}, Max: {1}").format(varray(min_value_2d, max_value_2d)));
	print_line(String("3D | Min: {0}, Max: {1}").format(varray(min_value_3d, max_value_3d)));
}

// Generic analysis for noise functions
template <typename F2, typename F3, typename FloatT>
void test_derivatives_tpl(F2 noise_func_2d, F3 noise_func_3d) {
	const int iterations = ITERATIONS;
	const int step_resolution_count = STEP_RESOLUTION_COUNT;
	const FloatT step_min = STEP_MIN;
	const FloatT step_max = STEP_MAX;

	print_line(String("Derivatives across step from {0} to {1}").format(varray(step_min, step_max)));

	const FloatT step_resolution_count_f = step_resolution_count;

	print_line(String("2D:").format(varray(step_min, step_max)));

	FloatT min_max_derivative = std::numeric_limits<FloatT>::max();

	for (int j = 0; j < step_resolution_count; ++j) {
		FloatT max_derivative = 0.0;
		const FloatT step = Math::lerp(0.0001, 0.001, static_cast<FloatT>(j) / step_resolution_count_f);

		for (int i = 0; i < iterations; ++i) {
			const FloatT x = Math::randd() * 2000.0 - 1000.0;
			const FloatT y = Math::randd() * 2000.0 - 1000.0;

			FloatT d = get_derivative(x, y, step, noise_func_2d);
			if (d > max_derivative) {
				max_derivative = d;
			}
		}

		max_derivative /= step;

		print_line(String::num_real(max_derivative));

		if (max_derivative < min_max_derivative) {
			min_max_derivative = max_derivative;
		}
	}

	print_line(String("Min max derivative: {0}").format(varray(min_max_derivative)));

	print_line(String("3D:").format(varray(step_min, step_max)));

	min_max_derivative = std::numeric_limits<FloatT>::max();

	for (int j = 0; j < step_resolution_count; ++j) {
		FloatT max_derivative = 0.0;
		const FloatT step = Math::lerp(0.0001, 0.001, static_cast<FloatT>(j) / step_resolution_count_f);

		for (int i = 0; i < iterations; ++i) {
			const FloatT x = Math::randd() * 2000.0 - 1000.0;
			const FloatT y = Math::randd() * 2000.0 - 1000.0;
			const FloatT z = Math::randd() * 2000.0 - 1000.0;

			FloatT d = get_derivative(x, y, z, step, noise_func_3d);
			if (d > max_derivative) {
				max_derivative = d;
			}
		}

		max_derivative /= step;

		print_line(String::num_real(max_derivative));

		if (max_derivative < min_max_derivative) {
			min_max_derivative = max_derivative;
		}
	}

	print_line(String("Min max derivative: {0}").format(varray(min_max_derivative)));
}

template <typename F3>
void test_derivatives_with_image(String fpath, double step, F3 noise_func_3d) {
	const double x_min = 500.0;
	const double y = 500.0;
	const double z_min = 500.0;

	const int size_x = 512;
	const int size_z = 512;

	const double image_step = 1.0;

	const double x_max = x_min + image_step;
	const double z_max = z_min + image_step;

	const double min_value = 0.0;
	const double max_value = 10.0;

	Ref<Image> im;
	im.instance();
	im->create(size_x, size_z, false, Image::FORMAT_RGB8);
	im->lock();

	for (int py = 0; py < size_z; ++py) {
		for (int px = 0; px < size_x; ++px) {
			const double x = Math::lerp(x_min, x_max, static_cast<double>(px) / static_cast<double>(size_x));
			const double z = Math::lerp(z_min, z_max, static_cast<double>(py) / static_cast<double>(size_z));
			const double d = get_derivative(x, y, z, step, noise_func_3d) / step;
			const double g = (d - min_value) / (max_value - min_value);
			im->set_pixel(px, py, Color(g, g, g));
		}
	}

	im->unlock();

	print_line(String("Saving {0}").format(varray(fpath)));
	im->save_png(fpath);
}

template <typename F3>
void test_derivatives_with_image(String fname, int steps_resolution, F3 noise_func_3d) {
	for (int i = 0; i < steps_resolution; ++i) {
		const double step =
				Math::lerp(STEP_MIN, STEP_MAX, static_cast<double>(i) / static_cast<double>(steps_resolution));
		String fpath = String("{0}_{1}.png").format(varray(fname, i));
		test_derivatives_with_image(fpath, step, noise_func_3d);
	}
}

template <typename F2, typename F3>
void test_noise(String name, int tests, F2 noise_func_2d, F3 noise_func_3d) {
	print_line(String("--- {0}:").format(varray(name)));

	if ((tests & TEST_MIN_MAX) == 1) {
		test_min_max<F2, F3, double>(noise_func_2d, noise_func_3d);
	}
	if ((tests & TEST_DERIVATIVES) == 1) {
		test_derivatives_tpl<F2, F3, double>(noise_func_2d, noise_func_3d);
		test_derivatives_with_image(name + "_3D", 10, noise_func_3d);
	}
}

void test_fnl_noise(fast_noise_lite::FastNoiseLite &fnl, String name, int tests) {
	test_noise(
			name, tests,
			[&fnl](double x, double y) { return fnl.GetNoise(x, y); },
			[&fnl](double x, double y, double z) { return fnl.GetNoise(x, y, z); });
}

void test_noises() {
	Ref<FastNoiseLite> noise;
	noise.instance();

	fast_noise_lite::FastNoiseLite fn;
	fn.SetFractalType(fast_noise_lite::FastNoiseLite::FractalType_None);
	fn.SetFrequency(1.f);

	osn_context osn_ctx;
	open_simplex_noise(131183, &osn_ctx);

	// According to OpenSimplex2 author, the 3D version is supposed to have a max derivative around 4.23718
	// https://www.wolframalpha.com/input/?i=max+d%2Fdx+32.69428253173828125+*+x+*+%28%280.6-x%5E2%29%5E4%29+from+-0.6+to+0.6
	// But empiric measures have shown it around 8. Discontinuities do exist in this noise though,
	// which makes this measuring harder (and the reason why multiple step sizes are used)

	fn.SetNoiseType(fast_noise_lite::FastNoiseLite::NoiseType_OpenSimplex2);
	test_fnl_noise(fn, "FNL_OpenSimplex2", TEST_MIN_MAX | TEST_DERIVATIVES);

	fn.SetNoiseType(fast_noise_lite::FastNoiseLite::NoiseType_OpenSimplex2S);
	test_fnl_noise(fn, "FNL_OpenSimplex2S", TEST_MIN_MAX | TEST_DERIVATIVES);

	fn.SetNoiseType(fast_noise_lite::FastNoiseLite::NoiseType_Perlin);
	test_fnl_noise(fn, "FNL_Perlin", TEST_MIN_MAX | TEST_DERIVATIVES);

	fn.SetNoiseType(fast_noise_lite::FastNoiseLite::NoiseType_Value);
	test_fnl_noise(fn, "FNL Value", TEST_MIN_MAX | TEST_DERIVATIVES);

	// ValueCubic seems to be below -1..1
	// 2D | Min: -0.714547, Max: 0.742197
	// 3D | Min: -0.542093, Max: 0.499036
	fn.SetNoiseType(fast_noise_lite::FastNoiseLite::NoiseType_ValueCubic);
	test_fnl_noise(fn, "FNL_ValueCubic", TEST_MIN_MAX | TEST_DERIVATIVES);

	fn.SetNoiseType(fast_noise_lite::FastNoiseLite::NoiseType_Cellular);

	const char *cell_distance_function_names[] = {
		"Euclidean",
		"EuclideanSq",
		"Manhattan",
		"Hybrid"
	};
	const char *cell_return_type_names[] = {
		"CellValue",
		"Distance",
		"Distance2",
		"Distance2Add",
		"Distance2Sub",
		"Distance2Mul",
		"Distance2Div"
	};

	for (int cell_distance_function = 0; cell_distance_function < 4; ++cell_distance_function) {
		for (int cell_return_type = 0; cell_return_type < 7; ++cell_return_type) {
			fn.SetCellularDistanceFunction(
					static_cast<fast_noise_lite::FastNoiseLite::CellularDistanceFunction>(cell_distance_function));
			fn.SetCellularReturnType(
					static_cast<fast_noise_lite::FastNoiseLite::CellularReturnType>(cell_return_type));

			const char *cell_distance_function_name = cell_distance_function_names[cell_distance_function];
			const char *cell_return_type_name = cell_return_type_names[cell_return_type];
			String noise_name =
					String("FNL_Cellular_{0}_{1}").format(varray(cell_distance_function_name, cell_return_type_name));

			const int jitter_resolution = 10;

			for (int i = 0; i < jitter_resolution; ++i) {
				const double jitter =
						Math::lerp(0.0, 1.0, static_cast<double>(i) / static_cast<double>(jitter_resolution));

				fn.SetCellularJitter(jitter);
				print_line(String("Cell jitter: {0}").format(varray(jitter)));

				test_fnl_noise(fn, noise_name, TEST_MIN_MAX);
			}
		}
	}

	test_noise(
			"OpenSimplex1", TEST_MIN_MAX | TEST_DERIVATIVES,
			[&osn_ctx](double x, double y) { return open_simplex_noise2(&osn_ctx, x, y); },
			[&osn_ctx](double x, double y, double z) { return open_simplex_noise3(&osn_ctx, x, y, z); });

	// Spreadsheet helper:
	print_line("Steps:");
	for (int i = 0; i < STEP_RESOLUTION_COUNT; ++i) {
		const double step =
				Math::lerp(STEP_MIN, STEP_MAX, static_cast<double>(i) / static_cast<double>(STEP_RESOLUTION_COUNT));
		print_line(String::num_real(step));
	}
}

} // namespace NoiseTests

#endif
