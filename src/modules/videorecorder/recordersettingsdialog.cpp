/**
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

#include "recordersettingsdialog.h"
#include "ui_recordersettingsdialog.h"

#include <QVariant>

RecorderSettingsDialog::RecorderSettingsDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::RecorderSettingsDialog),
    m_selectedImgSrcMod(nullptr)
{
    ui->setupUi(this);
    setWindowIcon(QIcon(":/icons/generic-config"));

    ui->containerComboBox->addItem("MKV", QVariant::fromValue(VideoContainer::Matroska));
    ui->containerComboBox->addItem("AVI", QVariant::fromValue(VideoContainer::AVI));
    ui->containerComboBox->setCurrentIndex(0);

    ui->codecComboBox->addItem("FFV1", QVariant::fromValue(VideoCodec::FFV1));
    ui->codecComboBox->addItem("AV1", QVariant::fromValue(VideoCodec::AV1));
    ui->codecComboBox->addItem("H.265", QVariant::fromValue(VideoCodec::H265));
    ui->codecComboBox->addItem("Raw", QVariant::fromValue(VideoCodec::Raw));
    ui->codecComboBox->setCurrentIndex(0);
}

RecorderSettingsDialog::~RecorderSettingsDialog()
{
    delete ui;
}

void RecorderSettingsDialog::setImageSourceModules(const QList<ImageSourceModule *> &mods)
{
    auto mod = m_selectedImgSrcMod;

    ui->frameSourceComboBox->clear();
    Q_FOREACH(auto mod, mods) {
        ui->frameSourceComboBox->addItem(mod->name(), QVariant(QMetaType::QObjectStar, &mod));
    }

    // ensure the right module is still selected
    for (int i = 0; i < ui->frameSourceComboBox->count(); i++) {
        if (ui->frameSourceComboBox->itemData(i).value<ImageSourceModule*>() == mod) {
            ui->frameSourceComboBox->setCurrentIndex(i);
            break;
        }
    }
    m_selectedImgSrcMod = mod;
}

ImageSourceModule *RecorderSettingsDialog::selectedImageSourceMod()
{
    return m_selectedImgSrcMod;
}

void RecorderSettingsDialog::setSelectedImageSourceMod(ImageSourceModule *mod)
{
    m_selectedImgSrcMod = mod;
}

void RecorderSettingsDialog::setVideoName(const QString &value)
{
    m_videoName = value.simplified().replace(" ", "_");
    ui->nameLineEdit->setText(m_videoName);
}

QString RecorderSettingsDialog::videoName() const
{
    return m_videoName;
}

void RecorderSettingsDialog::setSaveTimestamps(bool save)
{
    ui->timestampFileCheckBox->setChecked(save);
}

bool RecorderSettingsDialog::saveTimestamps() const
{
    return ui->timestampFileCheckBox->isChecked();
}

void RecorderSettingsDialog::setVideoCodec(const VideoCodec& codec)
{
    for (int i = 0; i < ui->codecComboBox->count(); i++) {
        if (ui->codecComboBox->itemData(i).value<VideoCodec>() == codec) {
            ui->codecComboBox->setCurrentIndex(i);
            break;
        }
    }
}

VideoCodec RecorderSettingsDialog::videoCodec() const
{
    return ui->codecComboBox->currentData().value<VideoCodec>();
}

void RecorderSettingsDialog::setVideoContainer(const VideoContainer& container)
{
    for (int i = 0; i < ui->containerComboBox->count(); i++) {
        if (ui->containerComboBox->itemData(i).value<VideoContainer>() == container) {
            ui->containerComboBox->setCurrentIndex(i);
            break;
        }
    }
}

VideoContainer RecorderSettingsDialog::videoContainer() const
{
    return ui->containerComboBox->currentData().value<VideoContainer>();
}

void RecorderSettingsDialog::setLossless(bool lossless)
{
    ui->losslessCheckBox->setChecked(lossless);
}

bool RecorderSettingsDialog::isLossless() const
{
    return ui->losslessCheckBox->isChecked();
}

void RecorderSettingsDialog::setSliceInterval(uint interval)
{
    ui->sliceIntervalSpinBox->setValue(static_cast<int>(interval));
}

uint RecorderSettingsDialog::sliceInterval() const
{
    return static_cast<uint>(ui->sliceIntervalSpinBox->value());
}

void RecorderSettingsDialog::on_frameSourceComboBox_currentIndexChanged(int index)
{
    Q_UNUSED(index);
    m_selectedImgSrcMod = ui->frameSourceComboBox->currentData().value<ImageSourceModule*>();
}

void RecorderSettingsDialog::on_nameLineEdit_textChanged(const QString &arg1)
{
    m_videoName = arg1.simplified().replace(" ", "_");
}

void RecorderSettingsDialog::on_codecComboBox_currentIndexChanged(int index)
{
    Q_UNUSED(index);

    // reset state of lossless infobox
    ui->losslessCheckBox->setEnabled(true);
    ui->losslessLabel->setEnabled(true);
    ui->losslessCheckBox->setChecked(true);
    ui->containerComboBox->setEnabled(true);

    const auto codec = ui->codecComboBox->currentData().value<VideoCodec>();

    if (codec == VideoCodec::FFV1) {
        // FFV1 is always lossless
        ui->losslessCheckBox->setEnabled(false);
        ui->losslessLabel->setEnabled(false);
        ui->losslessCheckBox->setChecked(true);

    } else if (codec == VideoCodec::H265) {
        // H.256 only works with MKV and MP4 containers, select MKV by default
        ui->containerComboBox->setCurrentIndex(0);
        ui->containerComboBox->setEnabled(false);

    } else if (codec == VideoCodec::MPEG4) {
        // MPEG-4 can't do lossless encoding
        ui->losslessCheckBox->setEnabled(false);
        ui->losslessLabel->setEnabled(false);
        ui->losslessCheckBox->setChecked(false);

    } else if (codec == VideoCodec::Raw) {
        // Raw is always lossless
        ui->losslessCheckBox->setEnabled(false);
        ui->losslessLabel->setEnabled(false);
        ui->losslessCheckBox->setChecked(true);

        // Raw RGB only works with AVI containers
        ui->containerComboBox->setCurrentIndex(1);
        ui->containerComboBox->setEnabled(false);
    }
}