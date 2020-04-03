#pragma once
#include <QDomElement>

#include "effects/defs.h"
#include "effects/effectmanifest.h"
#include "effects/presets/effectparameterpreset.h"

class EffectPreset {
  public:
    EffectPreset();
    EffectPreset(const QDomElement& element);
    EffectPreset(const EffectManifestPointer pManifest);
    ~EffectPreset();

  private:
    QString m_id;
    EffectBackendType m_backendType;
    double m_dMetaParameter;

    QList<EffectParameterPreset> m_effectParameterPresets;
};
