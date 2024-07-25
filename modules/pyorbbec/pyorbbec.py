# -*- coding: utf-8 -*-
'''
Orbbec Acquire Module for Syntalos

This module interfaces with Orbbec depth cameras and provides depth data
to other Syntalos modules.
'''

import syntalos_mlink as sy
#from sy import InputWaitResult, Frame
import numpy as np
from pyorbbecsdk import *
import json
import tkinter as tk

class OrbbecAcquire:
    def __init__(self):
        self._pipeline = None
        self._config = None
        self._oport_depth = None
        self._oport_metadata = None
        self._frame_index = 0
        self._settings = {}

    def prepare(self):
        try:
            # Initialize Orbbec pipeline and configuration
            self._pipeline = Pipeline()
            self._config = Config()

            # Configure depth stream
            profile_list = self._pipeline.get_stream_profile_list(OBSensorType.DEPTH_SENSOR)
            if not profile_list:
                raise RuntimeError('Orbbec device not connected. Please connect and try again.')
            
            depth_profile = profile_list.get_video_stream_profile(640, 480, OBFormat.Y16, 30)
            self._config.enable_stream(depth_profile)

            # Configure output ports
            self._oport_depth = sy.get_output_port('depth-out')
            self._oport_depth.set_metadata_value('type', 'depth')
            self._oport_depth.set_metadata_value('format', 'Y16')
            self._oport_depth.set_metadata_value('width', 640)
            self._oport_depth.set_metadata_value('height', 480)
            self._oport_depth.set_metadata_value('framerate', 30)

            self._oport_metadata = sy.get_output_port('metadata-out')
            self._oport_metadata.set_metadata_value('table_header', ['Frame Index', 'Timestamp (ms)'])

        except OBError as e:
            sy.raise_error(f"Orbbec initialization error: {str(e)}")

    def start(self):
        if self._pipeline is None or self._config is None:
            sy.raise_error('Orbbec pipeline or configuration not initialized.')
            return

        try:
            self._pipeline.start(self._config)
        except OBError as e:
            sy.raise_error(f"Failed to start Orbbec pipeline: {str(e)}")

    def loop(self) -> bool:
        try:
            frameset = self._pipeline.wait_for_frames(100)
            if not frameset:
                return True  # No frame available, but continue looping

            depth_frame = frameset.get_depth_frame()
            if depth_frame:
                depth_data = np.asarray(depth_frame.get_data())
                timestamp_ms = sy.time_since_start_msec()

                # Submit depth frame
                output_frame = Frame()
                output_frame.img = depth_data
                output_frame.index = self._frame_index
                output_frame.time_msec = timestamp_ms
                self._oport_depth.submit(output_frame)

                # Submit metadata
                self._oport_metadata.submit([self._frame_index, timestamp_ms])

                self._frame_index += 1

        except OBError as e:
            sy.raise_error(f"Orbbec runtime error: {str(e)}")
            return False

        return True

    def stop(self):
        if self._pipeline:
            self._pipeline.stop()

    def change_settings(self, old_settings: bytes) -> bytes:
        settings = json.loads(old_settings.decode('utf-8')) if old_settings else {}

        window = tk.Tk()
        window.title('Orbbec Acquire Settings')

        # Add settings fields here, e.g.:
        tk.Label(window, text='Exposure:').grid(row=0, column=0)
        exposure_entry = tk.Entry(window)
        exposure_entry.insert(0, str(settings.get('exposure', 0)))
        exposure_entry.grid(row=0, column=1)

        def save_settings():
            self._settings['exposure'] = int(exposure_entry.get())
            window.quit()

        tk.Button(window, text='Save', command=save_settings).grid(row=1, column=1)

        window.mainloop()
        window.destroy()

        return json.dumps(self._settings).encode('utf-8')

    def set_settings(self, settings_data: bytes):
        if settings_data:
            self._settings = json.loads(settings_data.decode('utf-8'))
        # Apply settings to Orbbec camera here

# Module-level functions
mod = OrbbecAcquire()

def set_settings(settings):
    mod.set_settings(settings)

def prepare():
    mod.prepare()

def start():
    mod.start()

def loop() -> bool:
    return mod.loop()

def stop():
    mod.stop()

def change_settings(old_settings: bytes) -> bytes:
    return mod.change_settings(old_settings)