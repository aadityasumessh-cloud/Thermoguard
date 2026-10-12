#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <thread>

#include "plant/pid.hpp"
#include "plant/thermal_plant.hpp"

using plant::PidConfig;
using plant::PidController;
using plant::ThermalPlant;

TEST(PidTest, RejectsInvalidOutputRange) {
    PidConfig config;
    config.out_min = 10.0;
    config.out_max = 10.0;
    EXPECT_THROW(PidController{config}, std::invalid_argument);
}

TEST(PidTest, ProportionalOnlyScalesError) {
    PidController pid({2.0, 0.0, 0.0, 0.0, 100.0});
    EXPECT_DOUBLE_EQ(pid.update(50.0, 40.0, 0.1), 20.0);  // kp * error
}

TEST(PidTest, OutputIsClamped) {
    PidController pid({10.0, 0.0, 0.0, 0.0, 100.0});
    EXPECT_DOUBLE_EQ(pid.update(90.0, 10.0, 0.1), 100.0);
    EXPECT_DOUBLE_EQ(pid.update(10.0, 90.0, 0.1), 0.0);
}

TEST(PidTest, IntegralAccumulatesWhileErrorPersists) {
    PidController pid({0.0, 1.0, 0.0, 0.0, 100.0});
    const double first = pid.update(10.0, 0.0, 1.0);
    const double second = pid.update(10.0, 0.0, 1.0);
    EXPECT_GT(second, first);
}

TEST(PidTest, AntiWindupRecoversQuicklyAfterSaturation) {
    PidController pid({0.0, 1.0, 0.0, 0.0, 100.0});
    for (int i = 0; i < 10000; ++i) {
        pid.update(1000.0, 0.0, 1.0);  // long saturation: a naive integral would explode
    }
    // Now the error reverses. Because the integral was clamped to 100, a handful
    // of steps is enough to bring the output back down.
    double output = 100.0;
    for (int i = 0; i < 20; ++i) {
        output = pid.update(0.0, 50.0, 10.0);
    }
    EXPECT_LT(output, 100.0);
}

TEST(PidTest, ZeroOrNegativeDtKeepsLastOutput) {
    PidController pid({1.0, 0.0, 0.0, 0.0, 100.0});
    const double output = pid.update(50.0, 40.0, 0.1);
    EXPECT_DOUBLE_EQ(pid.update(80.0, 10.0, 0.0), output);
}

TEST(PidTest, ResetClearsState) {
    PidController pid({0.0, 1.0, 0.0, 0.0, 100.0});
    pid.update(10.0, 0.0, 5.0);
    pid.reset();
    EXPECT_DOUBLE_EQ(pid.last_output(), 0.0);
    EXPECT_DOUBLE_EQ(pid.update(0.0, 0.0, 1.0), 0.0);
}

TEST(ThermalPlantTest, StartsAtInitialTemperature) {
    ThermalPlant plant(25.0, 0.6, 30.0, 25.0);
    EXPECT_DOUBLE_EQ(plant.temperature_c(), 25.0);
}

TEST(ThermalPlantTest, RejectsNonPositiveTimeConstant) {
    EXPECT_THROW(ThermalPlant(25.0, 0.6, 0.0, 25.0), std::invalid_argument);
}

TEST(ThermalPlantTest, ApproachesSteadyStateForConstantHeater) {
    ThermalPlant plant(25.0, 0.6, 30.0, 25.0);
    plant.set_heater_percent(50.0);
    plant.step(1000.0);  // many time constants
    EXPECT_NEAR(plant.temperature_c(), 25.0 + 0.6 * 50.0, 1e-6);
}

TEST(ThermalPlantTest, CoolsBackToAmbientWhenHeaterIsOff) {
    ThermalPlant plant(25.0, 0.6, 30.0, 80.0);
    plant.set_heater_percent(0.0);
    plant.step(1000.0);
    EXPECT_NEAR(plant.temperature_c(), 25.0, 1e-6);
}

TEST(ThermalPlantTest, HeaterIsClampedToValidRange) {
    ThermalPlant plant(25.0, 0.6, 30.0, 25.0);
    plant.set_heater_percent(500.0);
    EXPECT_DOUBLE_EQ(plant.heater_percent(), 100.0);
    plant.set_heater_percent(-5.0);
    EXPECT_DOUBLE_EQ(plant.heater_percent(), 0.0);
}

TEST(ThermalPlantTest, StepResultDoesNotDependOnStepSize) {
    ThermalPlant coarse(25.0, 0.6, 30.0, 25.0);
    ThermalPlant fine(25.0, 0.6, 30.0, 25.0);
    coarse.set_heater_percent(60.0);
    fine.set_heater_percent(60.0);
    coarse.step(20.0);
    for (int i = 0; i < 200; ++i) {
        fine.step(0.1);
    }
    EXPECT_NEAR(coarse.temperature_c(), fine.temperature_c(), 1e-9);
}

TEST(ThermalPlantTest, ConcurrentReadAndStepIsSafe) {
    ThermalPlant plant(25.0, 0.6, 30.0, 25.0);
    plant.set_heater_percent(50.0);
    std::thread stepper([&] {
        for (int i = 0; i < 20000; ++i) plant.step(0.01);
    });
    double last = 0.0;
    for (int i = 0; i < 20000; ++i) last = plant.temperature_c();
    stepper.join();
    EXPECT_GE(last, 25.0);
}

// Closed loop: PID + plant, simulated deterministically (no threads, no sleeping).
TEST(ClosedLoopTest, PidDrivesPlantToSetpointWithSmallOvershoot) {
    ThermalPlant plant(25.0, 0.6, 30.0, 25.0);
    PidController pid({4.0, 0.4, 0.0, 0.0, 100.0});
    const double setpoint = 50.0;
    const double dt = 0.5;

    double peak = 0.0;
    for (int i = 0; i < 1200; ++i) {  // 600 simulated seconds
        const double temperature = plant.temperature_c();
        peak = std::max(peak, temperature);
        plant.set_heater_percent(pid.update(setpoint, temperature, dt));
        plant.step(dt);
    }
    EXPECT_NEAR(plant.temperature_c(), setpoint, 0.5);
    EXPECT_LT(peak, setpoint + 8.0);
}

TEST(ClosedLoopTest, PidTracksASetpointChange) {
    ThermalPlant plant(25.0, 0.6, 30.0, 25.0);
    PidController pid({4.0, 0.4, 0.0, 0.0, 100.0});
    const double dt = 0.5;
    auto run = [&](double setpoint, int steps) {
        for (int i = 0; i < steps; ++i) {
            plant.set_heater_percent(pid.update(setpoint, plant.temperature_c(), dt));
            plant.step(dt);
        }
    };
    run(40.0, 1000);
    EXPECT_NEAR(plant.temperature_c(), 40.0, 0.5);
    run(60.0, 1400);
    EXPECT_NEAR(plant.temperature_c(), 60.0, 0.5);
}
