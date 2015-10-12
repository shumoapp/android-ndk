/*
 * Copyright 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.google.sample.echo;

import android.app.Activity;
import android.content.Context;
import android.database.Cursor;
import android.media.AudioManager;
import android.media.RingtoneManager;
import android.net.Uri;
import android.os.Bundle;
import android.provider.MediaStore;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.widget.TextView;

public class MainActivity extends Activity {
    public static final String AUDIO_SAMPLE = "AUDIO_SAMPLE:";
    TextView status_view;
    String  nativeSampleRate;
    String  nativeSampleBufSize;
    String  nativeSampleFormat;
    Boolean isPlaying;
    Boolean isDecoding;
    private String rintTonePath;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        status_view = (TextView)findViewById(R.id.statusView);
        queryNativeAudioParameters();

        Uri uri= RingtoneManager.getActualDefaultRingtoneUri(this, RingtoneManager.TYPE_RINGTONE);
        rintTonePath = getRealPathFromURI(uri);

        // initialize native audio system
        updateNativeAudioUI();
        NativeFastPlayer.createSLEngine(Integer.parseInt(nativeSampleRate), Integer.parseInt(nativeSampleBufSize));
        isPlaying = false;
        isDecoding = false;
    }
    @Override
    protected void onDestroy() {
        if( isPlaying ) {
            NativeFastPlayer.stopPlay();
        }
        //shutdown();
        NativeFastPlayer.deleteSLEngine();
        isPlaying = false;
        isDecoding = false;
        super.onDestroy();
    }

    private String getRealPathFromURI(Uri contentURI) {
        String result;
        Cursor cursor = getContentResolver().query(contentURI, null, null, null, null);
        if (cursor == null) { // Source is Dropbox or other similar local file path
            result = contentURI.getPath();
        } else {
            cursor.moveToFirst();
            int idx = cursor.getColumnIndex(MediaStore.Audio.AudioColumns.DATA);
            result = cursor.getString(idx);
            cursor.close();
        }
        return result;
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        // Inflate the menu; this adds items to the action bar if it is present.
        getMenuInflater().inflate(R.menu.menu_main, menu);
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        // Handle action bar item clicks here. The action bar will
        // automatically handle clicks on the Home/Up button, so long
        // as you specify a parent activity in AndroidManifest.xml.
        int id = item.getItemId();

        //noinspection SimplifiableIfStatement
        if (id == R.id.action_settings) {
            return true;
        }

        return super.onOptionsItemSelected(item);
    }

    public void startEcho(View view) {
        status_view.setText("StartCapture Button Clicked\n");
        if(isPlaying || isDecoding) {
            return;
        }
        if(!NativeFastPlayer.createSLBufferQueueAudioPlayer()) {
            status_view.setText("Failed to create Audio Player");
            return;
        }
        if(!NativeFastPlayer.createAudioRecorder()) {
            NativeFastPlayer.deleteSLBufferQueueAudioPlayer();
            status_view.setText("Failed to create Audio Recorder");
            return;
        }
        NativeFastPlayer.startPlay();   //this must include startRecording()
        isPlaying = true;
        status_view.setText("Engine Echoing ....");
    }

    public void stopEcho(View view) {
        if(!isPlaying && !isDecoding) {
            return;
        }
        NativeFastPlayer.stopPlay();  //this must include stopRecording()
        updateNativeAudioUI();
        NativeFastPlayer.deleteSLBufferQueueAudioPlayer();
        NativeFastPlayer.deleteAudioRecorder();
        isPlaying = false;
    }

    public void startRingtone(View view) {
        status_view.setText("StartCapture Button Clicked\n");
        if(isDecoding || isPlaying) {
            return;
        }
        if(!NativeFastPlayer.createSLBufferQueueAudioPlayer()) {
            status_view.setText("Failed to create Audio Player");
            return;
        }
        if(!NativeFastPlayer.createAudioDecoder(rintTonePath.getBytes())) {
            NativeFastPlayer.deleteSLBufferQueueAudioPlayer();
            status_view.setText("Failed to create Audio Recorder");
            return;
        }
        NativeFastPlayer.startPlay();   //this must include startDecoding()
        isDecoding = true;
        status_view.setText("Ringtone playing ...");
    }

    public void stopRingtone(View view) {
        if(!isDecoding && !isPlaying) {
            return;
        }
        NativeFastPlayer.stopPlay();  //this must include stopDecoding()
        updateNativeAudioUI();
        NativeFastPlayer.deleteSLBufferQueueAudioPlayer();
        NativeFastPlayer.deleteAudioRecorder();
        isDecoding = false;
    }

    public void getLowLatencyParameters(View view) {
        updateNativeAudioUI();
        return;
    }

    private void queryNativeAudioParameters() {
        AudioManager myAudioMgr = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
        nativeSampleRate  =  myAudioMgr.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE);
        nativeSampleBufSize =myAudioMgr.getProperty(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER);
        nativeSampleFormat ="";    //TODO: find a way to get the native audio format
    }
    private void updateNativeAudioUI() {
        status_view.setText("nativeSampleRate    = " + nativeSampleRate + "\n" +
                "nativeSampleBufSize = " + nativeSampleBufSize + "\n" +
                "nativeSampleFormat  = " + nativeSampleFormat);

    }
}
