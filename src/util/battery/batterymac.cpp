#include <QtDebug>

#include "util/battery/batterymac.h"
#include "util/mac.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>

BatteryMac::BatteryMac(QObject* pParent)
        : Battery(pParent) {
}

BatteryMac::~BatteryMac() {
}

void BatteryMac::read() {
    m_iMinutesLeft = 0;
    m_iPercentage = 0;
    m_csChargingState = Battery::UNKNOWN;

    CFTypeRef powerInfo = IOPSCopyPowerSourcesInfo();
    if (powerInfo == NULL) {
        return;
    }

    CFArrayRef powerSources = IOPSCopyPowerSourcesList(powerInfo);

    if (powerSources == NULL) {
        CFRelease(powerSources);
        return;
    }

    int sourceCount = CFArrayGetCount(powerSources);
    for (int i = 0; i < sourceCount; ++i) {
        CFDictionaryRef pSource = IOPSGetPowerSourceDescription(
            powerInfo, CFArrayGetValueAtIndex(powerSources, i));

        if (pSource == NULL) {
            continue;
        }

        QString name;
        bool on_ac = false;
        bool is_charging = false;
        bool is_charged = false;
        int current_capacity = 0;
        int max_capacity = 0;
        int minutes_left = -1;

        const void* pValue = CFDictionaryGetValue(pSource, CFSTR(kIOPSNameKey));
        if (pValue != NULL) {
            name = CFStringToQString((CFStringRef)pValue);
        }

        // Property required by Apple spec.
        pValue = CFDictionaryGetValue(pSource, CFSTR(kIOPSPowerSourceStateKey));
        if (CFStringCompare((CFStringRef)pValue,
                            CFSTR(kIOPSOffLineValue), 0) == 0) {
            // Source is not connected. Skip to the next one.
            continue;
        } else if (CFStringCompare((CFStringRef)pValue,
                                   CFSTR(kIOPSACPowerValue), 0) == 0) {
            on_ac = true;
        } else if (CFStringCompare((CFStringRef)pValue,
                            CFSTR(kIOPSBatteryPowerValue), 0) == 0) {
            on_ac = false;
        } else {
            // This is not a valid value of kIOPSPowerSourceStateKey.
        }

        // Property required by Apple spec.
        pValue = CFDictionaryGetValue(pSource, CFSTR(kIOPSIsChargingKey));
        if (pValue != NULL) {
            is_charging = CFBooleanGetValue((CFBooleanRef)pValue);
        }

        // Property required by Apple spec.
        pValue = CFDictionaryGetValue(pSource, CFSTR(kIOPSIsChargedKey));
        if (pValue != NULL) {
            is_charged = CFBooleanGetValue((CFBooleanRef)pValue);
        }


        // Property required by Apple spec.
        pValue = CFDictionaryGetValue(pSource, CFSTR(kIOPSCurrentCapacityKey));
        if (pValue != NULL) {
            CFNumberGetValue((CFNumberRef)pValue, kCFNumberSInt32Type,
                             &current_capacity);
        }

        // Property required by Apple spec.
        pValue = CFDictionaryGetValue(pSource, CFSTR(kIOPSMaxCapacityKey));
        if (pValue != NULL) {
            CFNumberGetValue((CFNumberRef)pValue, kCFNumberSInt32Type,
                             &max_capacity);
        }

        // Property optional by Apple spec.
        if (is_charging) {
            pValue = CFDictionaryGetValue(pSource, CFSTR(kIOPSTimeToFullChargeKey));
            if (pValue != NULL) {
                CFNumberGetValue((CFNumberRef)pValue, kCFNumberSInt32Type,
                                 &minutes_left);
            }
        } else if (!on_ac && !is_charging) {
            pValue = CFDictionaryGetValue(pSource, CFSTR(kIOPSTimeToEmptyKey));
            if (pValue != NULL) {
                CFNumberGetValue((CFNumberRef)pValue, kCFNumberSInt32Type,
                                 &minutes_left);
            }
        }

        if (max_capacity > 0) {
            m_iPercentage = 100 * current_capacity / max_capacity;
        }

        if (on_ac) {
            m_csChargingState = is_charging ? CHARGING : CHARGED;
        } else {
            m_csChargingState = DISCHARGING;
        }

        if (minutes_left != -1) {
            m_iMinutesLeft = minutes_left;
        }

        // qDebug() << "BatteryMac::read()" << sourceCount << i << name
        //          << "capacity" << current_capacity << max_capacity
        //          << "on_ac" << on_ac
        //          << "charging" << is_charging
        //          << "charged" << is_charged
        //          << "minutes_left" << minutes_left;
        break;
    }

    CFRelease(powerSources);
    CFRelease(powerInfo);
}
