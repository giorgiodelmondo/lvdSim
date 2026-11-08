// SPDX-License-Identifier: GPL-3.0-or-later
// The waveform renderer policy: environment + stored setting + platform -> what to request.
#include "wave/render_policy.hpp"

#include <QtTest>

using namespace lvd::gui;

class TestRenderPolicy : public QObject {
    Q_OBJECT

    static RenderDecision decide(const QString& env, const QString& setting, const QString& platform = "windows") {
        return resolveRenderPolicy(env, setting, platform);
    }

private slots:
    void defaultIsAutomaticGpu() {
        const RenderDecision d = decide({}, {});
        QCOMPARE(d.policy, RenderPolicy::Auto);
        QVERIFY(d.requestGpu);
        QVERIFY(d.cpuReason.isEmpty());
    }

    void environmentValues() {
        QCOMPARE(decide("cpu", {}).policy, RenderPolicy::Cpu);
        QVERIFY(!decide("cpu", {}).requestGpu);
        QCOMPARE(decide("QPainter", {}).policy, RenderPolicy::Cpu);
        QCOMPARE(decide(" CPU ", {}).policy, RenderPolicy::Cpu);
        QCOMPARE(decide("gpu", {}).policy, RenderPolicy::Gpu);
        QVERIFY(decide("gpu", {}).requestGpu);
        QCOMPARE(decide("auto", {}).policy, RenderPolicy::Auto);
    }

    void garbageEnvironmentIsIgnored() {
        QCOMPARE(decide("banana", {}).policy, RenderPolicy::Auto);
        QCOMPARE(decide("banana", "cpu").policy, RenderPolicy::Cpu);
        QCOMPARE(decide("", "gpu").policy, RenderPolicy::Gpu);
    }

    void settingValues() {
        QCOMPARE(decide({}, "auto").policy, RenderPolicy::Auto);
        QCOMPARE(decide({}, "cpu").policy, RenderPolicy::Cpu);
        QCOMPARE(decide({}, "gpu").policy, RenderPolicy::Gpu);
        QCOMPARE(decide({}, "nonsense").policy, RenderPolicy::Auto);
    }

    void environmentBeatsSetting() {
        QCOMPARE(decide("cpu", "gpu").policy, RenderPolicy::Cpu);
        QCOMPARE(decide("gpu", "cpu").policy, RenderPolicy::Gpu);
        QCOMPARE(decide("auto", "cpu").policy, RenderPolicy::Auto);
    }

    void headlessPlatformsNeverRequestTheGpu() {
        for (const char* platform : {"offscreen", "minimal", "vnc"}) {
            for (const char* env : {"", "auto", "gpu"}) {
                const RenderDecision d = decide(env, {}, platform);
                QVERIFY(!d.requestGpu);
                QCOMPARE(d.cpuReason, QString("platform %1").arg(platform));
            }
        }
        // A deliberate QPainter choice has nothing to explain.
        QVERIFY(decide("cpu", {}, "offscreen").cpuReason.isEmpty());
        for (const char* platform : {"windows", "cocoa", "xcb", "wayland"})
            QVERIFY(decide({}, {}, platform).requestGpu);
    }

    void namesRoundTrip() {
        for (RenderPolicy p : {RenderPolicy::Auto, RenderPolicy::Cpu, RenderPolicy::Gpu}) {
            RenderPolicy back = RenderPolicy::Auto;
            QVERIFY(parseRenderPolicy(renderPolicyName(p), &back));
            QCOMPARE(back, p);
        }
        QVERIFY(!parseRenderPolicy("", nullptr));
    }
};

QTEST_MAIN(TestRenderPolicy)
#include "test_render_policy.moc"
