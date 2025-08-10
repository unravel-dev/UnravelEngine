#pragma once
#include <vector>
#include <string>
#include <functional>
#include <utility>

namespace seq
{
namespace ease
{

float linear(float progress);

float smooth_start(float progress);
float smooth_stop(float progress);
float smooth_start_stop(float progress);

float smooth_start2(float progress);
float smooth_stop2(float progress);
float smooth_start_stop2(float progress);

float smooth_start3(float progress);
float smooth_stop3(float progress);
float smooth_start_stop3(float progress);

float smooth_start4(float progress);
float smooth_stop4(float progress);
float smooth_start_stop4(float progress);

float smooth_start5(float progress);
float smooth_stop5(float progress);
float smooth_start_stop5(float progress);

float smooth_start6(float progress);
float smooth_stop6(float progress);
float smooth_start_stop6(float progress);

float circular_start(float progress);
float circular_stop(float progress);
float circular_start_stop(float progress);

float elastic_start(float progress);
float elastic_stop(float progress);
float elastic_start_stop(float progress);

float back_start(float progress);
float back_stop(float progress);
float back_start_stop(float progress);

float bounce_start(float progress);
float bounce_stop(float progress);
float bounce_start_stop(float progress);

float arch(float progress);
float arch_smooth_step(float progress);
float arch_smooth_start_stop(float progress);
float arch_smooth_start(float progress);
float arch_smooth_stop(float progress);

std::function<float(float)> create_back_stop(float overshoot = 1.70158f);
std::function<float(float)> create_back_start(float overshoot = 1.70158f);
std::function<float(float)> create_back_stop_stop(float overshoot = 1.70158f);

const std::vector<std::pair<std::string, std::function<float(float)>>>& get_ease_list();

}
}
