/*
 * Copyright (C) 2020-2024 Matthias Klumpp <matthias@tenstral.net>
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

#pragma once

#include "datactl/frametype.h"
#include <QIcon>
#include <QObject>
#include <QWidget>

class QLabel;

/**
 * @brief Interface for all transformation classes
 */
class VideoTransform : public QObject
{
public:
    explicit VideoTransform();

    virtual QString name() const = 0;
    virtual QIcon icon() const
    {
        return QIcon::fromTheme("view-filter");
    };
    virtual void createSettingsUi(QWidget *parent) = 0;

    void setOriginalSize(const QSize &size);
    virtual QSize resultSize();

    virtual bool allowOnlineModify() const;

    virtual void start();
    virtual void process(cv::Mat &image) = 0;
    virtual void stop();

    virtual QVariantHash toVariantHash();
    virtual void fromVariantHash(const QVariantHash &settings);

protected:
    QSize m_originalSize;
};

/**
 * @brief Crop frames to match a certain size
 */
class CropTransform : public VideoTransform
{
    Q_OBJECT
public:
    explicit CropTransform();

    QString name() const override;
    QIcon icon() const override;
    void createSettingsUi(QWidget *parent) override;

    bool allowOnlineModify() const override;
    QSize resultSize() override;

    void start() override;
    void process(cv::Mat &image) override;

    QVariantHash toVariantHash() override;
    void fromVariantHash(const QVariantHash &settings) override;

private:
    void checkAndUpdateRoi();

    QLabel *m_sizeInfoLabel;

    std::mutex m_mutex;

    QSize m_maxima;
    cv::Size m_activeOutSize;
    cv::Rect m_roi;
    cv::Rect m_activeRoi;
    std::atomic_bool m_onlineModified;
};

/**
 * @brief Crop frames to match a certain size
 */
class ScaleTransform : public VideoTransform
{
    Q_OBJECT
public:
    explicit ScaleTransform();

    QString name() const override;
    QIcon icon() const override;
    void createSettingsUi(QWidget *parent) override;

    QSize resultSize() override;
    void process(cv::Mat &image) override;

    QVariantHash toVariantHash() override;
    void fromVariantHash(const QVariantHash &settings) override;

private:
    double m_scaleFactor;
};

/**
 * @brief Apply a false-color transformation to the video
 */
class FalseColorTransform : public VideoTransform
{
    Q_OBJECT
public:
    explicit FalseColorTransform();

    QString name() const override;
    QIcon icon() const override;

    void createSettingsUi(QWidget *parent) override;

    void process(cv::Mat &image) override;
};

/**
 * @brief Apply a histogram normalization transformation to the video
 */
class HistNormTransform : public VideoTransform
{
    Q_OBJECT
public:
    explicit HistNormTransform();

    QString name() const override;
    QIcon icon() const override;

    void createSettingsUi(QWidget *parent) override;

    void process(cv::Mat &image) override;
};
