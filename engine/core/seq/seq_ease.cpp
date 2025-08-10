#include "seq_ease.h"

#include "seq_math.h"

#include <cmath>

namespace
{
constexpr float pi = 3.14159265358979323846f;
constexpr float half_pi = pi * 0.5f;

float linearInterpolation(float a)
{
    return a;
}

float quadraticEaseIn(float a)
{
    return a * a;
}

float quadraticEaseOut(float a)
{
    return -(a * (a - static_cast<float>(2)));
}

float quadraticEaseInOut(float a)
{
    if(a < static_cast<float>(0.5))
    {
        return static_cast<float>(2) * a * a;
    }

    return (-static_cast<float>(2) * a * a) + (4 * a) - 1.0f;
}

float cubicEaseIn(float a)
{
    return a * a * a;
}

float cubicEaseOut(float a)
{
    float const f = a - 1.0f;
    return f * f * f + 1.0f;
}

float cubicEaseInOut(float a)
{
    if(a < static_cast<float>(0.5))
    {
        return static_cast<float>(4) * a * a * a;
    }

    float const f = ((static_cast<float>(2) * a) - static_cast<float>(2));
    return static_cast<float>(0.5) * f * f * f + 1.0f;
}

float quarticEaseIn(float a)
{
    return a * a * a * a;
}

float quarticEaseOut(float a)
{
    float const f = (a - 1.0f);
    return f * f * f * (1.0f - a) + 1.0f;
}

float quarticEaseInOut(float a)
{
    if(a < static_cast<float>(0.5))
    {
        return static_cast<float>(8) * a * a * a * a;
    }

    float const f = (a - 1.0f);
    return -static_cast<float>(8) * f * f * f * f + 1.0f;
}

float quinticEaseIn(float a)
{
    return a * a * a * a * a;
}

float quinticEaseOut(float a)
{
    float const f = (a - 1.0f);
    return f * f * f * f * f + 1.0f;
}

float quinticEaseInOut(float a)
{
    if(a < static_cast<float>(0.5))
    {
        return static_cast<float>(16) * a * a * a * a * a;
    }

    float const f = ((static_cast<float>(2) * a) - static_cast<float>(2));
    return static_cast<float>(0.5) * f * f * f * f * f + 1.0f;
}

float sineEaseIn(float a)
{
    return std::sin((a - 1.0f) * half_pi) + 1.0f;
}

float sineEaseOut(float a)
{
    return std::sin(a * half_pi);
}

float sineEaseInOut(float a)
{
    return static_cast<float>(0.5) * (1.0f - std::cos(a * static_cast<float>(pi)));
}

float circularEaseIn(float a)
{
    return 1.0f - std::sqrt(1.0f - (a * a));
}

float circularEaseOut(float a)
{
    return std::sqrt((static_cast<float>(2) - a) * a);
}

float circularEaseInOut(float a)
{
    if(a < static_cast<float>(0.5))
    {
        return static_cast<float>(0.5) * (1.0f - std::sqrt(1.0f - static_cast<float>(4) * (a * a)));
    }

    return static_cast<float>(0.5) *
           (std::sqrt(-((static_cast<float>(2) * a) - static_cast<float>(3)) * ((static_cast<float>(2) * a) - 1.0f)) +
            1.0f);
}

float exponentialEaseIn(float a)
{
    if(a <= 0.0f)
    {
        return a;
    }

    float const Complementary = a - 1.0f;
    float const Two = 2.0f;

    return std::pow(Two, Complementary * static_cast<float>(10));
}

float exponentialEaseOut(float a)
{
    if(a >= 1.0f)
    {
        return a;
    }

    return 1.0f - std::pow(static_cast<float>(2), -static_cast<float>(10) * a);
}

float exponentialEaseInOut(float a)
{
    if(a < static_cast<float>(0.5))
    {
        return static_cast<float>(0.5) *
               std::pow(static_cast<float>(2), (static_cast<float>(20) * a) - static_cast<float>(10));
    }

    return -static_cast<float>(0.5) *
               std::pow(static_cast<float>(2), (-static_cast<float>(20) * a) + static_cast<float>(10)) +
           1.0f;
}

float elasticEaseIn(float a)
{
    return std::sin(static_cast<float>(13) * half_pi * a) *
           std::pow(static_cast<float>(2), static_cast<float>(10) * (a - 1.0f));
}

float elasticEaseOut(float a)
{
    return std::sin(-static_cast<float>(13) * half_pi * (a + 1.0f)) *
               std::pow(static_cast<float>(2), -static_cast<float>(10) * a) +
           1.0f;
}

float elasticEaseInOut(float a)
{
    if(a < static_cast<float>(0.5))
    {
        return static_cast<float>(0.5) * std::sin(static_cast<float>(13) * half_pi * (static_cast<float>(2) * a)) *
               std::pow(static_cast<float>(2), static_cast<float>(10) * ((static_cast<float>(2) * a) - 1.0f));
    }

    return static_cast<float>(0.5) *
           (std::sin(-static_cast<float>(13) * half_pi * ((static_cast<float>(2) * a - 1.0f) + 1.0f)) *
                std::pow(static_cast<float>(2), -static_cast<float>(10) * (static_cast<float>(2) * a - 1.0f)) +
            static_cast<float>(2));
}

float backEaseIn(float a, float const& o)
{
    float z = ((o + 1.0f) * a) - o;
    return (a * a * z);
}

float backEaseOut(float a, float const& o)
{
    float n = a - 1.0f;
    float z = ((o + 1.0f) * n) + o;
    return (n * n * z) + 1.0f;
}

float backEaseInOut(float a, float const& o)
{
    float s = o * static_cast<float>(1.525);
    float x = 0.5f;
    float n = a / static_cast<float>(0.5);

    if(n < static_cast<float>(1))
    {
        float z = ((s + static_cast<float>(1)) * n) - s;
        float m = n * n * z;
        return x * m;
    }

    n -= static_cast<float>(2);
    float z = ((s + static_cast<float>(1)) * n) + s;
    float m = (n * n * z) + static_cast<float>(2);
    return x * m;
}

float backEaseIn(float a)
{
    return backEaseIn(a, static_cast<float>(1.70158));
}

float backEaseOut(float a)
{
    return backEaseOut(a, static_cast<float>(1.70158));
}

float backEaseInOut(float a)
{
    return backEaseInOut(a, static_cast<float>(1.70158));
}

float bounceEaseOut(float a)
{
    if(a < static_cast<float>(4.0 / 11.0))
    {
        return (static_cast<float>(121) * a * a) / static_cast<float>(16);
    }
    if(a < static_cast<float>(8.0 / 11.0))
    {
        return (static_cast<float>(363.0 / 40.0) * a * a) - (static_cast<float>(99.0 / 10.0) * a) +
               static_cast<float>(17.0 / 5.0);
    }
    if(a < static_cast<float>(9.0 / 10.0))
    {
        return (static_cast<float>(4356.0 / 361.0) * a * a) - (static_cast<float>(35442.0 / 1805.0) * a) +
               static_cast<float>(16061.0 / 1805.0);
    }
    return (static_cast<float>(54.0 / 5.0) * a * a) - (static_cast<float>(513.0 / 25.0) * a) +
           static_cast<float>(268.0 / 25.0);
}

float bounceEaseIn(float a)
{
    return 1.0f - bounceEaseOut(1.0f - a);
}

float bounceEaseInOut(float a)
{
    if(a < static_cast<float>(0.5))
    {
        return static_cast<float>(0.5) * (1.0f - bounceEaseOut(a * static_cast<float>(2)));
    }

    return static_cast<float>(0.5) * bounceEaseOut(a * static_cast<float>(2) - 1.0f) + static_cast<float>(0.5);
}

} // end of anonymous namespace

namespace seq
{
namespace ease
{

float linear(float progress)
{
    return linearInterpolation(progress);
}

/// Modelled after quarter-cycle of sine wave
float smooth_start(float progress)
{
    return sineEaseIn(progress);
}

/// Modelled after quarter-cycle of sine wave (different phase)
float smooth_stop(float progress)
{
    return sineEaseOut(progress);
}

/// Modelled after half sine wave
float smooth_start_stop(float progress)
{
    return sineEaseInOut(progress);
}

/// Modelled after the parabola y = x^2
float smooth_start2(float progress)
{
    return quadraticEaseIn(progress);
}

/// Modelled after the parabola y = -x^2 + 2x
float smooth_stop2(float progress)
{
    return quadraticEaseOut(progress);
}

/// Modelled after the piecewise quadratic
/// y = (1/2)((2x)^2)				; [0, 0.5)
/// y = -(1/2)((2x-1)*(2x-3) - 1)	; [0.5, 1]
float smooth_start_stop2(float progress)
{
    return quadraticEaseInOut(progress);
}

/// Modelled after the cubic y = x^3
float smooth_start3(float progress)
{
    return cubicEaseIn(progress);
}

/// Modelled after the cubic y = (x - 1)^3 + 1
float smooth_stop3(float progress)
{
    return cubicEaseOut(progress);
}

/// Modelled after the piecewise cubic
/// y = (1/2)((2x)^3)		; [0, 0.5)
/// y = (1/2)((2x-2)^3 + 2)	; [0.5, 1]
float smooth_start_stop3(float progress)
{
    return cubicEaseInOut(progress);
}

/// Modelled after the quartic x^4
float smooth_start4(float progress)
{
    return quarticEaseIn(progress);
}

/// Modelled after the quartic y = 1 - (x - 1)^4
float smooth_stop4(float progress)
{
    return quarticEaseOut(progress);
}

/// Modelled after the piecewise quartic
/// y = (1/2)((2x)^4)			; [0, 0.5)
/// y = -(1/2)((2x-2)^4 - 2)	; [0.5, 1]
float smooth_start_stop4(float progress)
{
    return quarticEaseInOut(progress);
}

/// Modelled after the quintic y = x^5
float smooth_start5(float progress)
{
    return quinticEaseIn(progress);
}

/// Modelled after the quintic y = (x - 1)^5 + 1
float smooth_stop5(float progress)
{
    return quinticEaseOut(progress);
}

/// Modelled after the piecewise quintic
/// y = (1/2)((2x)^5)		; [0, 0.5)
/// y = (1/2)((2x-2)^5 + 2) ; [0.5, 1]
float smooth_start_stop5(float progress)
{
    return quinticEaseInOut(progress);
}

/// Modelled after the exponential function y = 2^(10(x - 1))
float smooth_start6(float progress)
{
    return exponentialEaseIn(progress);
}

/// Modelled after the exponential function y = -2^(-10x) + 1
float smooth_stop6(float progress)
{
    return exponentialEaseOut(progress);
}

/// Modelled after the piecewise exponential
/// y = (1/2)2^(10(2x - 1))			; [0,0.5)
/// y = -(1/2)*2^(-10(2x - 1))) + 1 ; [0.5,1]
float smooth_start_stop6(float progress)
{
    return exponentialEaseInOut(progress);
}

/// Modelled after shifted quadrant IV of unit circle
float circular_start(float progress)
{
    return circularEaseIn(progress);
}

/// Modelled after shifted quadrant II of unit circle
float circular_stop(float progress)
{
    return circularEaseOut(progress);
}

/// Modelled after the piecewise circular function
/// y = (1/2)(1 - sqrt(1 - 4x^2))			; [0, 0.5)
/// y = (1/2)(sqrt(-(2x - 3)*(2x - 1)) + 1) ; [0.5, 1]
float circular_start_stop(float progress)
{
    return circularEaseInOut(progress);
}

/// Modelled after the damped sine wave y = sin(13pi/2*x)*pow(2, 10 * (x - 1))
float elastic_start(float progress)
{
    return elasticEaseIn(progress);
}

/// Modelled after the damped sine wave y = sin(-13pi/2*(x + 1))*pow(2, -10x) + 1
float elastic_stop(float progress)
{
    return elasticEaseOut(progress);
}

/// Modelled after the piecewise exponentially-damped sine wave:
/// y = (1/2)*sin(13pi/2*(2*x))*pow(2, 10 * ((2*x) - 1))		; [0,0.5)
/// y = (1/2)*(sin(-13pi/2*((2x-1)+1))*pow(2,-10(2*x-1)) + 2)	; [0.5, 1]
float elastic_start_stop(float progress)
{
    return elasticEaseInOut(progress);
}

float back_start(float progress)
{
    return backEaseIn(progress);
}

float back_stop(float progress)
{
    return backEaseOut(progress);
}

float back_start_stop(float progress)
{
    return backEaseInOut(progress);
}

float bounce_start(float progress)
{
    return bounceEaseIn(progress);
}

float bounce_stop(float progress)
{
    return bounceEaseOut(progress);
}

float bounce_start_stop(float progress)
{
    return bounceEaseInOut(progress);
}

float arch(float progress)
{
    return progress * (1.0f - progress) * 4.0f;
}

float arch_smooth_step(float progress)
{
    return reverse_scale(scale(arch(progress), progress), progress) * 4.0f;
}

float arch_smooth_start_stop(float progress)
{
    return arch_smooth_start(progress) * arch_smooth_stop(progress);
}

float arch_smooth_start(float progress)
{
    return progress * progress * (1.0f - progress) * 8.0f;
}

float arch_smooth_stop(float progress)
{
    const auto remaining = 1.0f - progress;
    return progress * remaining * remaining * 8.0f;
}

std::function<float(float)> create_back_start(float overshoot)
{
    return [overshoot](float a)
    {
        return backEaseIn(a, overshoot);
    };
}

std::function<float(float)> create_back_stop(float overshoot)
{
    return [overshoot](float a)
    {
        return backEaseOut(a, overshoot);
    };
}

std::function<float(float)> create_back_stop_stop(float overshoot)
{
    return [overshoot](float a)
    {
        return backEaseInOut(a, overshoot);
    };
}

const std::vector<std::pair<std::string, std::function<float(float)>>>& get_ease_list()
{
    static const std::vector<std::pair<std::string, std::function<float(float)>>> list = {
        {"linear", linear},

        {"smooth_start", smooth_start},
        {"smooth_start2", smooth_start2},
        {"smooth_start3", smooth_start3},
        {"smooth_start4", smooth_start4},
        {"smooth_start5", smooth_start5},
        {"smooth_start6", smooth_start6},

        {"smooth_stop", smooth_stop},
        {"smooth_stop2", smooth_stop2},
        {"smooth_stop3", smooth_stop3},
        {"smooth_stop4", smooth_stop4},
        {"smooth_stop5", smooth_stop5},
        {"smooth_stop6", smooth_stop6},

        {"smooth_start_stop", smooth_start_stop},
        {"smooth_start_stop2", smooth_start_stop2},
        {"smooth_start_stop3", smooth_start_stop3},
        {"smooth_start_stop4", smooth_start_stop4},
        {"smooth_start_stop5", smooth_start_stop5},
        {"smooth_start_stop6", smooth_start_stop6},

        {"circular_start", circular_start},
        {"circular_stop", circular_stop},
        {"circular_start_stop", circular_start_stop},

        {"elastic_start", elastic_start},
        {"elastic_stop", elastic_stop},
        {"elastic_start_stop", elastic_start_stop},

        {"back_start", back_start},
        {"back_stop", back_stop},
        {"back_start_stop", back_start_stop},

        {"bounce_start", bounce_start},
        {"bounce_stop", bounce_stop},
        {"bounce_start_stop", bounce_start_stop},

        {"arch", arch},
        {"arch_smooth_step", arch_smooth_step},
        {"arch_smooth_start_stop", arch_smooth_start_stop},
        {"arch_smooth_start", arch_smooth_start},
        {"arch_smooth_stop", arch_smooth_stop},
    };
    return list;
}

} // namespace ease
} // namespace seq
