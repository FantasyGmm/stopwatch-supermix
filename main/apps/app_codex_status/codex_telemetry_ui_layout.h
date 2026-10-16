#pragma once


namespace codex_telemetry_ui {

constexpr int kScreenSize = 466;
constexpr int kPageIndicatorY = 416;
constexpr int kContentBottom = 404;
constexpr int kHeaderY = 27;
constexpr int kStatusY = 66;

constexpr int kUsageWeekY = 100;
constexpr int kUsageProvidersY = 145;
constexpr int kUsageTodayHeadingY = 170;
constexpr int kUsageTodayValueY = 188;
constexpr int kUsageModelPositionY = 218;
constexpr int kUsageModelY = 237;
constexpr int kUsageModelSummaryY = 265;
constexpr int kUsageBucketsFirstY = 294;
constexpr int kUsageBucketsSecondY = 318;
constexpr int kUsagePerformanceHeadingY = 343;
constexpr int kUsagePerformanceValueY = 360;
constexpr int kUsageSamplesY = 388;

constexpr int kHostCpuY = 112;
constexpr int kHostMemoryY = 188;
constexpr int kHostDiskY = 264;
constexpr int kHostNetworkY = 342;

static_assert(kHeaderY < kStatusY);
static_assert(kUsageWeekY < kUsageProvidersY);
static_assert(kUsageProvidersY < kUsageTodayHeadingY);
static_assert(kUsageTodayHeadingY < kUsageTodayValueY);
static_assert(kUsageTodayValueY < kUsageModelPositionY);
static_assert(kUsageModelPositionY < kUsageModelY);
static_assert(kUsageModelY < kUsageModelSummaryY);
static_assert(kUsageModelSummaryY < kUsageBucketsFirstY);
static_assert(kUsageBucketsFirstY < kUsageBucketsSecondY);
static_assert(kUsageBucketsSecondY < kUsagePerformanceHeadingY);
static_assert(kUsagePerformanceHeadingY < kUsagePerformanceValueY);
static_assert(kUsagePerformanceValueY < kUsageSamplesY);
static_assert(kUsageSamplesY + 15 <= kContentBottom);
static_assert(kHostCpuY < kHostMemoryY);
static_assert(kHostMemoryY < kHostDiskY);
static_assert(kHostDiskY < kHostNetworkY);
static_assert(kHostNetworkY + 43 < kContentBottom);
static_assert(kContentBottom < kPageIndicatorY);

}  // namespace codex_telemetry_ui
