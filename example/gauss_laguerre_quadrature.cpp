///////////////////////////////////////////////////////////////////////////////
//      Copyright Christopher Kormanyos 2012 - 2015, 2020.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
//

#include <cmath>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <tuple>
#include <vector>

#include <boost/cstdfloat.hpp>
#include <boost/math/constants/constants.hpp>
#include <boost/math/special_functions/cbrt.hpp>
#include <boost/math/special_functions/bessel.hpp>
#include <boost/math/special_functions/factorials.hpp>
#include <boost/math/special_functions/gamma.hpp>
#include <boost/math/tools/roots.hpp>
#include <boost/noncopyable.hpp>

#define CPP_BIN_FLOAT  1
#define CPP_DEC_FLOAT  2
#define CPP_MPFR_FLOAT 3

//#define MP_TYPE CPP_BIN_FLOAT
#define MP_TYPE CPP_DEC_FLOAT
//#define MP_TYPE CPP_MPFR_FLOAT

#if (MP_TYPE == CPP_BIN_FLOAT)
  #include <boost/multiprecision/cpp_bin_float.hpp>
#elif (MP_TYPE == CPP_DEC_FLOAT)
  #include <boost/multiprecision/cpp_dec_float.hpp>
#elif (MP_TYPE == CPP_MPFR_FLOAT)
  #include <boost/multiprecision/mpfr.hpp>
#else
  #error MP_TYPE is undefined
#endif

namespace gauss { namespace laguerre {
namespace detail
{
  template<typename T>
  class laguerre_l_object final
  {
  public:
    laguerre_l_object(const int n, const T a) noexcept
      : order(n),
        alpha(a),
        p1   (0),
        d2   (0) { }

    laguerre_l_object& operator=(const laguerre_l_object& other)
    {
      if(this != other)
      {
        order = other.order;
        alpha = other.alpha;
        p1    = other.p1;
        d2    = other.d2;
      }

      return *this;
    }

    T operator()(const T& x) const noexcept
    {
      // Calculate (via forward recursion):
      // * the value of the Laguerre function L(n, alpha, x), called (p2),
      // * the value of the derivative of the Laguerre function (d2),
      // * and the value of the corresponding Laguerre function of
      //   previous order (p1).

      // Return the value of the function (p2) in order to be used as a
      // function object with Boost.Math root-finding. Store the values
      // of the Laguerre function derivative (d2) and the Laguerre function
      // of previous order (p1) in class members for later use.

        p1 = T(0);
      T p2 = T(1);
        d2 = T(0);

      T j_plus_alpha(alpha);
      T two_j_plus_one_plus_alpha_minus_x((1 + alpha) - x);

      const T my_two = 2;

      for(int j = 0; j < order; ++j)
      {
        const T p0(p1);

        // Set the value of the previous Laguerre function.
        p1 = p2;

        // Use a recurrence relation to compute the value of the Laguerre function.
        p2 = ((two_j_plus_one_plus_alpha_minus_x * p1) - (j_plus_alpha * p0)) / (j + 1);

        ++j_plus_alpha;
        two_j_plus_one_plus_alpha_minus_x += my_two;
      }

      // Set the value of the derivative of the Laguerre function.
      d2 = ((p2 * order) - (j_plus_alpha * p1)) / x;

      // Return the value of the Laguerre function.
      return p2;
    }

    const T previous  () const noexcept { return p1; }
    const T derivative() const noexcept { return d2; }

    static bool root_tolerance(const T& a, const T& b)
    {
      using std::fabs;

      // The relative tolerance here is: ((a - b) * 2) / (a + b).
      return ((fabs(a - b) * 2) < (fabs(a + b) * std::numeric_limits<T>::epsilon()));
    }

  private:
    const   int order;
    const   T   alpha;
    mutable T   p1;
    mutable T   d2;
  };

  template<typename T>
  class abscissas_and_weights : private boost::noncopyable
  {
  public:
    abscissas_and_weights(const int n, const T a) : order(n),
                                                    alpha(a),
                                                    xi   (),
                                                    wi   ()
    {
      if(alpha < -20.0F)
      {
        // If we boostify this, one could throw a range error here.
        // If so, then also document it in the docs.
        std::cout << "Range error: the order of the Laguerre function must exceed -20.0." << std::endl;
      }
      else
      {
        calculate();
      }
    }

    const std::vector<T>& abscissa_n() const noexcept { return xi; }
    const std::vector<T>& weight_n  () const noexcept { return wi; }

  private:
    const int order;
    const T   alpha;

    std::vector<T> xi;
    std::vector<T> wi;

    abscissas_and_weights() = default;

    void calculate()
    {
      using std::abs;

      std::cout << "Finding the approximate roots..." << std::endl;

      std::vector<std::tuple<T, T>> root_estimates;

      root_estimates.reserve(static_cast<typename std::vector<std::tuple<T, T>>::size_type>(order));

      const laguerre_l_object<T> laguerre_root_object(order, alpha);

      // Set the initial values of the step size and the running step
      // to be used for finding the estimate of the first root.
      T step_size  = 0.01F;
      T step       = step_size;

      T first_laguerre_root = 0.0F;

      if(alpha < -1.0F)
      {
        // Iteratively step through the Laguerre function using a
        // small step-size in order to find a rough estimate of
        // the first zero.

        const bool this_laguerre_value_is_negative = (laguerre_root_object(T(0)) < 0);

        BOOST_CONSTEXPR_OR_CONST int j_max = 10000;

        int j = 0;

        while((j < j_max) && (this_laguerre_value_is_negative != (laguerre_root_object(step) < 0)))
        {
          // Increment the step size until the sign of the Laguerre function
          // switches. This indicates a zero-crossing, signalling the next root.
          step += step_size;

          ++j;
        }
      }
      else
      {
        // Calculate an estimate of the 1st root of a generalized Laguerre
        // function using either a Taylor series or an expansion in Bessel
        // function zeros. The Bessel function zeros expansion is from Tricomi.

        // Here, we obtain an estimate of the first zero of cyl_bessel_j(alpha, x).

        T j_alpha_m1;

        if(alpha < 1.4F)
        {
          // For small alpha, use a short series obtained from Mathematica(R).
          // Series[BesselJZero[v, 1], {v, 0, 3}]
          // N[%, 12]
          j_alpha_m1 = (((          T(0.09748661784476F)
                          * alpha - T(0.17549359276115F))
                          * alpha + T(1.54288974259931F))
                          * alpha + T(2.40482555769577F));
        }
        else
        {
          // For larger alpha, use the first line of Eqs. 10.21.40 in the NIST Handbook.
          const T alpha_pow_third(boost::math::cbrt(alpha));
          const T alpha_pow_minus_two_thirds(T(1) / (alpha_pow_third * alpha_pow_third));

          j_alpha_m1 = alpha * (((((                             + T(0.043F)
                                    * alpha_pow_minus_two_thirds - T(0.0908F))
                                    * alpha_pow_minus_two_thirds - T(0.00397F))
                                    * alpha_pow_minus_two_thirds + T(1.033150F))
                                    * alpha_pow_minus_two_thirds + T(1.8557571F))
                                    * alpha_pow_minus_two_thirds + T(1.0F));
        }

        const T vf             = (  T(order * 4U)
                                  + T(alpha * 2U)
                                  + T(2U));
        const T vf2            = vf * vf;
        const T j_alpha_m1_sqr = j_alpha_m1 * j_alpha_m1;

        first_laguerre_root = (j_alpha_m1_sqr * (   -T(0.6666666666667F)
                                                 + ((T(0.6666666666667F) * alpha) * alpha)
                                                 +  (T(0.3333333333333F) * j_alpha_m1_sqr) + vf2)) / (vf2 * vf);
      }

      bool this_laguerre_value_is_negative = (laguerre_root_object(T(0)) < 0);

      // Re-set the initial value of the step-size based on the
      // estimate of the first root.
      step_size = first_laguerre_root / 2;
      step      = step_size;

      // Step through the Laguerre function using a step-size
      // of dynamic width in order to find the zero crossings
      // of the Laguerre function, providing rough estimates
      // of the roots. Refine the brackets with a few bisection
      // steps, and store the results as bracketed root estimates.

      while(root_estimates.size() < static_cast<std::size_t>(order))
      {
        // Increment the step size until the sign of the Laguerre function
        // switches. This indicates a zero-crossing, signalling the next root.
        step += step_size;

        if(this_laguerre_value_is_negative != (laguerre_root_object(step) < 0))
        {
          // We have found the next zero-crossing.

          // Change the running sign of the Laguerre function.
          this_laguerre_value_is_negative = (!this_laguerre_value_is_negative);

          // We have found the first zero-crossing. Put a loose bracket around
          // the root using a window. Here, we know that the first root lies
          // between (x - step_size) < root < x.

          // Before storing the approximate root, perform a couple of
          // bisection steps in order to tighten up the root bracket.
          boost::uintmax_t a_couple_of_iterations = 4U;

          const std::pair<T, T>
            root_estimate_bracket = boost::math::tools::bisect(laguerre_root_object,
                                                               step - step_size,
                                                               step,
                                                               laguerre_l_object<T>::root_tolerance,
                                                               a_couple_of_iterations);

          static_cast<void>(a_couple_of_iterations);

          // Store the refined root estimate as a bracketed range in a tuple.
          root_estimates.push_back(std::tuple<T, T>(std::get<0>(root_estimate_bracket),
                                                    std::get<1>(root_estimate_bracket)));

          if(root_estimates.size() >= static_cast<std::size_t>(2U))
          {
            // Determine the next step size. This is based on the distance between
            // the previous two roots, whereby the estimates of the previous roots
            // are computed by taking the average of the lower and upper range of
            // the root-estimate bracket.

            const T r0 = (  std::get<0>(*(root_estimates.rbegin() + 1U))
                          + std::get<1>(*(root_estimates.rbegin() + 1U))) / 2;

            const T r1 = (  std::get<0>(*root_estimates.rbegin())
                          + std::get<1>(*root_estimates.rbegin())) / 2;

            const T distance_between_previous_roots = r1 - r0;

            step_size = distance_between_previous_roots / 3;
          }
        }
      }

      const T norm_g =
        ((alpha == 0) ? T(-1)
                      : -boost::math::tgamma(alpha + order) / boost::math::factorial<T>(order - 1));

      xi.reserve(root_estimates.size());
      wi.reserve(root_estimates.size());

      // Calculate the abscissas and weights to full precision.
      for(std::size_t i = static_cast<std::size_t>(0U); i < root_estimates.size(); ++i)
      {
        std::cout << "Calculating abscissa and weight for index: " << i << std::endl;

        // Calculate the abscissas using iterative root-finding.

        // Select the maximum allowed iterations, being at least 20.
        // The determination of the maximum allowed iterations is
        // based on the number of decimal digits in the numerical
        // type T.
        BOOST_CONSTEXPR_OR_CONST int my_digits10 =
          static_cast<int>(static_cast<boost::float_least32_t>(boost::math::tools::digits<T>()) * BOOST_FLOAT32_C(0.301));

        const boost::uintmax_t number_of_iterations_allowed = (std::max)(20, my_digits10 / 2);

        boost::uintmax_t number_of_iterations_used = number_of_iterations_allowed;

        // Perform the root-finding using ACM TOMS 748 from Boost.Math.
        const std::pair<T, T>
          laguerre_root_bracket = boost::math::tools::toms748_solve(laguerre_root_object,
                                                                    std::get<0>(root_estimates.at(i)),
                                                                    std::get<1>(root_estimates.at(i)),
                                                                    laguerre_l_object<T>::root_tolerance,
                                                                    number_of_iterations_used);

        static_cast<void>(number_of_iterations_used);

        // Compute the Laguerre root as the average of the values from
        // the solved root bracket.
        const T laguerre_root = (  std::get<0>(laguerre_root_bracket)
                                 + std::get<1>(laguerre_root_bracket)) / 2;

        // Calculate the weight for this Laguerre root. Here, we calculate
        // the derivative of the Laguerre function and the value of the
        // previous Laguerre function on the x-axis at the value of this
        // Laguerre root.
        static_cast<void>(laguerre_root_object(laguerre_root));

        // Store the abscissa and weight for this index.
        xi.push_back(laguerre_root);
        wi.push_back(norm_g / ((laguerre_root_object.derivative() * order) * laguerre_root_object.previous()));
      }
    }
  };

  template<typename T>
  struct airy_ai_object final
  {
  public:
    airy_ai_object(const T& x) : my_x  (x),
                                 zeta  (make_zeta()),
                                 factor(make_factor()) { }

    T operator()(const T& t) const
    {
      using std::sqrt;

      return factor / sqrt(boost::math::cbrt(2 + (t / zeta)));
    }

  private:
    const T my_x;
    const T zeta;
    const T factor;

    airy_ai_object() = default;

    T make_zeta()
    {
      using std::sqrt;

      return ((sqrt(my_x) * my_x) * 2) / 3;
    }

    T make_factor()
    {
      using std::exp;
      using std::sqrt;

      const T zeta_times_48_pow_sixth = sqrt(boost::math::cbrt(zeta * 48));

      return 1 / ((sqrt(boost::math::constants::pi<T>()) * zeta_times_48_pow_sixth) * (exp(zeta) * boost::math::tgamma(T(5) / 6)));
    }
  };
} // namespace detail

} } // namespace gauss::laguerre

namespace local
{
  struct digits_characteristics
  {
    BOOST_STATIC_CONSTEXPR unsigned int my_digits10       = 100U;
    BOOST_STATIC_CONSTEXPR unsigned int my_guard_digits10 =   6U;
    BOOST_STATIC_CONSTEXPR unsigned int my_total_digits10 = my_digits10 + my_guard_digits10;
  };

  #if (MP_TYPE == CPP_BIN_FLOAT)
    using float_type =
      boost::multiprecision::number<boost::multiprecision::cpp_bin_float<digits_characteristics::my_total_digits10>,
                                    boost::multiprecision::et_off>;
  #elif (MP_TYPE == CPP_DEC_FLOAT)
    using float_type =
      boost::multiprecision::number<boost::multiprecision::cpp_dec_float<digits_characteristics::my_total_digits10>,
                                    boost::multiprecision::et_off>;
  #elif (MP_TYPE == CPP_MPFR_FLOAT)
    using float_type =
    boost::multiprecision::number<boost::multiprecision::mpfr_float_backend<digits_characteristics::my_total_digits10>,
                                  boost::multiprecision::et_off>;
  #else
    #error MP_TYPE is undefined
  #endif
}

int main()
{
  // Use Gauss-Laguerre integration to compute airy_ai(120 / 7).
  // We empirically find factors to relate the number of Gauss-Laguerre
  // coefficients needed for convergence when using varying base-10 digits.

  // Empirical data:
  //    d       laguerre_order_factor
  //    50             0.4
  //   100             0.6
  //   300             1.3
  //   500             2.0
  //  1000             3.6

  // Calibrate the number of coefficients needed at the point x = 120/7.
  // This leads to:
  // Fit[{{50.0,0.4}, {100.0,0.6}, {300.0,1.3}, {500.0,2.0}, {1000.0,3.8}, {1200.0,4.6}}, {1, d, d^2}, d]
  // 0.247299 + (0.0034126 + 1.67064*10^-7 d) d

  // Subsequently use a factor of 40.0 when computing smaller Airy function values as low as x >= 1.
  // We need significantly more coefficients at smaller values because the derivative of airy_ai(x)
  // is steeper as x approaches zero.

  // Basically then, this Gauss-Laguerre quadrature is designed for airy_ai(x) with x >= 1.

  BOOST_CONSTEXPR_OR_CONST boost::float_least32_t d = static_cast<boost::float_least32_t>(std::numeric_limits<local::float_type>::digits10);

  BOOST_CONSTEXPR_OR_CONST boost::float_least32_t laguerre_order_factor_raw = 0.247299F + ((0.0034126F + (1.67064E-7F * d)) * d);

  BOOST_CONSTEXPR_OR_CONST boost::float_least32_t laguerre_order_factor = laguerre_order_factor_raw * 40.0F;

  BOOST_CONSTEXPR_OR_CONST int laguerre_order = static_cast<int>(laguerre_order_factor * d);

  std::cout << "laguerre_order: " << laguerre_order << std::endl;

  const gauss::laguerre::detail::abscissas_and_weights<local::float_type> the_abscissas_and_weights(laguerre_order, -1.0F / local::float_type(6U));

  bool result_is_ok = true;

  for(std::uint32_t u = 7U; u < 121U; ++u)
  {
    const local::float_type x = local::float_type(u) / 7;

    const gauss::laguerre::detail::airy_ai_object<local::float_type> this_gauss_laguerre_ai(x);

    const local::float_type airy_ai_value =
      std::inner_product(the_abscissas_and_weights.abscissa_n().cbegin(),
                         the_abscissas_and_weights.abscissa_n().cend(),
                         the_abscissas_and_weights.weight_n().cbegin(),
                         local::float_type(0U),
                         std::plus<local::float_type>(),
                         [&this_gauss_laguerre_ai](const local::float_type& this_abscissa,
                                                   const local::float_type& this_weight) -> local::float_type
                         {
                           return this_gauss_laguerre_ai(this_abscissa) * this_weight;
                         });

    static const local::float_type one_third = 1.0F / local::float_type(3U);

    static const local::float_type one_over_pi_times_one_over_sqrt_three =
      sqrt(one_third) * boost::math::constants::one_div_pi<local::float_type>();

    const local::float_type sqrt_x = sqrt(x);

    const local::float_type airy_ai_control =
       (sqrt_x * one_over_pi_times_one_over_sqrt_three)
      * boost::math::cyl_bessel_k(one_third, ((2.0F * x) * sqrt_x) * one_third);

    std::cout << std::setprecision(local::digits_characteristics::my_digits10)
              << "airy_ai_value  : "
              << airy_ai_value
              << std::endl;

    std::cout << std::setprecision(local::digits_characteristics::my_digits10)
              << "airy_ai_control: "
              << airy_ai_control
              << std::endl;

    const local::float_type delta = fabs(1.0F - (airy_ai_control / airy_ai_value));

    static const local::float_type tol =
        std::numeric_limits<local::float_type>::epsilon()
      * local::float_type("1" + std::string(local::digits_characteristics::my_guard_digits10 + 5U, '0'));

    result_is_ok &= (delta < tol);
  }

  std::cout << "result_is_ok: " << std::boolalpha << result_is_ok << std::endl;
}
