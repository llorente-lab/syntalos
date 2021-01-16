/*
 * Copyright (C) 2019-2020 Matthias Klumpp <matthias@tenstral.net>
 *
 * Licensed under the GNU General Public License Version 3
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the license, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include "modulelibrary.h"

#include <QMessageBox>
#include <QDebug>
#include <QCoreApplication>
#include <QDirIterator>
#include <QLibrary>

#include "moduleapi.h"
#include "utils/tomlutils.h"


namespace Syntalos {
    Q_LOGGING_CATEGORY(logModLibrary, "modulelibrary")
}

class ModuleLocation
{
public:
    QString path;
    explicit ModuleLocation(const QString &dir)
        : path(dir) {};
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpadded"
class ModuleLibrary::Private
{
public:
    Private() { }
    ~Private() { }

    QString syntalosApiId;
    QList<ModuleLocation> locations;
    QMap<QString, QSharedPointer<ModuleInfo>> modInfos;

    QStringList issueLog;
};
#pragma GCC diagnostic pop

ModuleLibrary::ModuleLibrary(QObject *parent)
    : QObject(parent),
      d(new ModuleLibrary::Private)
{
    d->syntalosApiId = QStringLiteral(SY_VCS_TAG);

    bool haveLocalModDir = false;
    if (!QCoreApplication::applicationDirPath().startsWith("/usr")) {
        const auto path = QDir(QStringLiteral("%1/%2").arg(QCoreApplication::applicationDirPath()).arg("../modules")).canonicalPath();
        if (QDir(path).exists()) {
            d->locations.append(ModuleLocation(path));
            haveLocalModDir = true;
        }
    }

    // we only want to load the global system modules directory if we are not
    // loading the local one, to prevent name clashes and confusion
    if (!haveLocalModDir) {
        if (QDir(SY_MODULESDIR).exists())
            d->locations.append(ModuleLocation(SY_MODULESDIR));
    }
}

ModuleLibrary::~ModuleLibrary()
{
}

bool ModuleLibrary::load()
{
    for (const auto &loc : d->locations) {
        qCDebug(logModLibrary).noquote() << "Loading modules from location:" << loc.path;
        d->issueLog.append(QStringLiteral("Loading modules from: %1").arg(loc.path));

        int count = 0;
        QDirIterator it(loc.path, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::NoIteratorFlags);
        while (it.hasNext()) {
            const auto modDir = it.next();
            const auto modName = QFileInfo(modDir).fileName();

            qCDebug(logModLibrary).noquote() << "Loading:" << modName;
            QString errorMessage;
            QVariantHash modDef = parseTomlFile(QDir(modDir).filePath("module.toml"), errorMessage);
            if (modDef.isEmpty()) {
                qCWarning(logModLibrary).noquote().nospace() << "Unable to load module '" << modName << "': " << errorMessage;
                logModuleIssue(modName, "toml", errorMessage);
                continue;
            }
            modDef = modDef["syntalos_module"].toHash();
            if (modDef.value("type") == "library") {
                if (loadLibraryModInfo(modName, QDir(modDir).filePath(modDef.value("main").toString())))
                    count++;
            } else {
                qCWarning(logModLibrary).noquote().nospace() << "Unable to load module '" << modName << "': "
                                                             << "Module type is unknown.";
                logModuleIssue(modName, "toml", "Not found.");
            }
        }
        d->issueLog.append(QStringLiteral("Loaded %1 modules.").arg(count));
    }

    return true;
}

bool ModuleLibrary::loadLibraryModInfo(const QString &modName, const QString &libFname)
{
    typedef ModuleInfo* (*SyntalosModInfoFn)();
    typedef const char* (*SyntalosModAPIIdFn)();

    QLibrary modLib(libFname);

    modLib.setLoadHints(QLibrary::ResolveAllSymbolsHint | QLibrary::ExportExternalSymbolsHint);
    if (!modLib.load()) {
        qCWarning(logModLibrary).noquote().nospace() << "Unable to load library for module '" << modName << "': "
                                                     << modLib.errorString();
        logModuleIssue(modName, "lib", modLib.errorString());
        return false;
    }

    auto fnAPIId = (SyntalosModAPIIdFn) modLib.resolve("syntalos_module_api_id");
    if (fnAPIId == nullptr) {
        qCWarning(logModLibrary).noquote().nospace() << "Unable to load library for module '" << modName << "': "
                                                     << "Library is not a Syntalos module, 'syntalos_module_api_id' symbol not found.";
        logModuleIssue(modName, "api", "'syntalos_module_api_id' not found.");
        return false;
    }

    auto fnModInfo = (SyntalosModInfoFn) modLib.resolve("syntalos_module_info");
    if (fnModInfo == nullptr) {
        qCWarning(logModLibrary).noquote().nospace() << "Unable to load library for module '" << modName << "': "
                                                     << "Library is not a Syntalos module, 'syntalos_module_info' symbol not found.";
        logModuleIssue(modName, "api", "'syntalos_module_info' not found.");
        return false;
    }

    const auto modApiId = QString::fromUtf8(fnAPIId());
    if (modApiId != d->syntalosApiId) {
        const auto apiMismatchError = QStringLiteral("API ID mismatch between module and engine: %1 vs %2").arg(modApiId).arg(d->syntalosApiId);
        qCWarning(logModLibrary).noquote().nospace() << "Prevented module load for '" << modName << "': "
                                                     << apiMismatchError;
        logModuleIssue(modName, "api", apiMismatchError);
        return false;
    }

    // now we can load the module info object from the module's shared library
    auto modInfo = static_cast<ModuleInfo*> (fnModInfo());
    if (modInfo == nullptr) {
        qCWarning(logModLibrary).noquote().nospace() << "Prevented module load for '" << modName << "': "
                                                     << "Received invalid (NULL) module info data.";
        logModuleIssue(modName, "api", "Module info was NULL");
        return false;
    }

    // register
    QSharedPointer<ModuleInfo> info(modInfo);
    d->modInfos.insert(info->id(), info);
    return true;
}

void ModuleLibrary::logModuleIssue(const QString &modName, const QString &context, const QString &msg)
{
    d->issueLog.append(QStringLiteral("<b>%1</b>: <i>&lt;%2&gt;</i> %3").arg(modName).arg(context).arg(msg));
}

QList<QSharedPointer<ModuleInfo> > ModuleLibrary::moduleInfo() const
{
    return d->modInfos.values();
}

QSharedPointer<ModuleInfo> ModuleLibrary::moduleInfo(const QString &id)
{
    return d->modInfos.value(id);
}

QString ModuleLibrary::issueLogHtml() const
{
    return d->issueLog.join("<br/>");
}
