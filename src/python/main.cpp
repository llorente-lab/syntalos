/*
 * Copyright (C) 2019-2024 Matthias Klumpp <matthias@tenstral.net>
 *
 * Licensed under the GNU Lesser General Public License Version 3
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the license, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <QApplication>

#include <signal.h>
#include <sys/prctl.h>

#include "pyworker.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    if (a.arguments().length() >= 2 && a.arguments()[1] == "--doc") {
        if (a.arguments().length() != 3) {
            qCritical().noquote() << "Documentation: Invalid amount of arguments!";
            return 2;
        }
        PyWorker::makeDocFileAndQuit(a.arguments()[2]);
        return a.exec();
    }

    // never auto-quit when last window is closed, as the hosted script
    // may want to show transient Qt windows
    a.setQuitOnLastWindowClosed(false);

    // Initialize link to Syntalos. There can only be one.
    auto slink = initSyntalosModuleLink();
    auto worker = std::make_unique<PyWorker>(slink.get(), &a);
    worker->awaitData(1000);

    // ensure that this process dies with its parent
    prctl(PR_SET_PDEATHSIG, SIGKILL);

    // set the process name to the instance ID, to simplify identification in process trees
    prctl(PR_SET_NAME, qPrintable(slink->instanceId()), 0, 0, 0);

    return a.exec();
}
