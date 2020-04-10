/*
 * Copyright (C) 2016-2020 Matthias Klumpp <matthias@tenstral.net>
 *
 * Licensed under the GNU Lesser General Public License Version 3
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the license, or
 * (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this software.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "utils.h"

QString createRandomString(int len)
{
    const auto possibleCahrs = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");

    QString str;
    for (int i=0; i < len; i++) {
        int index = qrand() % possibleCahrs.length();
        QChar nextChar = possibleCahrs.at(index);
        str.append(nextChar);
    }

    return str;
}

QString simplifyStringForFilebasename(const QString &s)
{
    return s.simplified()
            .replace("/", "-")
            .replace("\\", "-")
            .replace(" ", "")
            .replace(":", "");
}
