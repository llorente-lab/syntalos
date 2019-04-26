/*
 * Copyright (C) 2016-2019 Matthias Klumpp <matthias@tenstral.net>
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

#include <opencv2/core.hpp>
#include "abstractmodule.h"

AbstractModule::AbstractModule(QObject *parent) :
    QObject(parent),
    m_state(ModuleState::PREPARING)
{

}

ModuleState AbstractModule::state() const
{
    return m_state;
}

bool AbstractModule::runCycle()
{
    return true;
}

bool AbstractModule::runThreads()
{
    return true;
}

void AbstractModule::finalize()
{
    // Do nothing.
}

void AbstractModule::showDisplayUi()
{
    // Do nothing.
}

void AbstractModule::showSettingsUi()
{
    // Do nothing
}

QString AbstractModule::lastError() const
{
    return m_lastError;
}

void AbstractModule::receiveFrame(const cv::Mat &frame, const std::chrono::milliseconds &timestamp)
{
    Q_UNUSED(frame);
    Q_UNUSED(timestamp);
    return;
}

void AbstractModule::setState(ModuleState state)
{
    m_state = state;
    emit stateChanged(state);
}

void AbstractModule::setLastError(const QString &message)
{
    m_lastError = message;
    emit errorMessage(message);
}