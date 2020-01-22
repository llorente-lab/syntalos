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

#pragma once

#include <QObject>
#include <QQueue>
#include <QTimer>

#include "rep_interface_source.h"
#include "sharedmemory.h"
#include "ipctypes.h"

class OOPWorker : public OOPWorkerSource
{
    Q_OBJECT
public:
    OOPWorker(QObject *parent = nullptr);
    ~OOPWorker() override;

    bool ready() const override;

public slots:
    bool initializeFromData(const QString & script, const QString & env) override;
    bool initializeFromFile(const QString & fname, const QString & env) override;

    void setInputPortInfo(QList<InputPortInfo> ports) override;
    void setOutputPortInfo(QList<OutputPortInfo> ports) override;

    void shutdown() override;

    void run();

    bool submitInput(int inPortId) override;

protected:
    void raiseError(const QString &message);

private:
    bool m_running;
    QList<std::shared_ptr<SharedMemory>> m_shmSend;
    QList<std::shared_ptr<SharedMemory>> m_shmRecv;

    QList<InputPortInfo> m_inPortInfo;
    QList<OutputPortInfo> m_outPortInfo;
};
