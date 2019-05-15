/*
 * Copyright (C) 2019 Matthias Klumpp <matthias@tenstral.net>
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

#include <QCoreApplication>

#include <QTimer>
#include "pycontroller.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    auto pyc = new PyController(&a);

    QObject::connect(pyc, &PyController::finished, &a, QCoreApplication::exit);
    QTimer::singleShot(0, pyc, &PyController::run);

    return a.exec();
}
