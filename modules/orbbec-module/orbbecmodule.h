#pragma once

#include "moduleapi.h"
#include <QObject>

SYNTALOS_DECLARE_MODULE

class OrbbecModuleInfo : public ModuleInfo
{
public:
    QString id() const final;
    QString name() const final;
    QString description() const final;
    ModuleCategories categories() const final;
    AbstractModule *createModule(QObject *parent = nullptr) final;
};
