package com.example.myempty.myesp32;

import android.Manifest;
import android.annotation.SuppressLint;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.content.pm.PackageManager;
import android.content.Intent;
import android.graphics.Color;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;
import androidx.appcompat.app.AppCompatActivity;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Set;
import java.util.UUID;

public class MainActivity extends AppCompatActivity {
    private static final int REQUEST_BLUETOOTH = 10;
    private static final UUID SPP_UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");

    private BluetoothAdapter bluetoothAdapter;
    private BluetoothSocket socket;
    private OutputStream output;
    private TextView connectionStatus;
    private Spinner deviceSpinner;
    private Spinner patternSpinner;
    private boolean updatingPattern;
    private final ArrayList<BluetoothDevice> devices = new ArrayList<>();
    private final Handler handler = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        bluetoothAdapter = BluetoothAdapter.getDefaultAdapter();
        connectionStatus = findViewById(R.id.tvConnectionStatus);
        deviceSpinner = findViewById(R.id.spinnerDevices);
        patternSpinner = findViewById(R.id.spinnerPattern);
        findViewById(R.id.btnConnect).setOnClickListener(view -> connectSelectedDevice());
        setupControls();

        if (bluetoothAdapter == null) {
            showMessage("Perangkat tidak mendukung Bluetooth");
        } else if (android.os.Build.VERSION.SDK_INT >= 31 &&
                checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.BLUETOOTH_CONNECT, Manifest.permission.BLUETOOTH_SCAN}, REQUEST_BLUETOOTH);
        } else {
            prepareBluetooth();
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != REQUEST_BLUETOOTH) return;

        boolean granted = grantResults.length > 0;
        for (int result : grantResults) granted &= result == PackageManager.PERMISSION_GRANTED;
        if (granted) prepareBluetooth();
        else showMessage("Izin Bluetooth diperlukan untuk memilih ESP32");
    }

    @SuppressLint("MissingPermission")
    private void prepareBluetooth() {
        if (!bluetoothAdapter.isEnabled()) {
            startActivityForResult(new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE), REQUEST_BLUETOOTH + 1);
            return;
        }
        loadPairedDevices();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_BLUETOOTH + 1) {
            if (resultCode == RESULT_OK) loadPairedDevices();
            else showMessage("Aktifkan Bluetooth untuk memilih ESP32");
        }
    }

    @SuppressLint("MissingPermission")
    private void loadPairedDevices() {
        devices.clear();
        Set<BluetoothDevice> paired = bluetoothAdapter.getBondedDevices();
        ArrayList<String> labels = new ArrayList<>();
        for (BluetoothDevice device : paired) {
            devices.add(device);
            labels.add(device.getName() + "\n" + device.getAddress());
        }
        if (labels.isEmpty()) labels.add("Belum ada perangkat dipasangkan");
        ArrayAdapter<String> adapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, labels);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        deviceSpinner.setAdapter(adapter);
        deviceSpinner.setEnabled(!devices.isEmpty());
    }

    @SuppressLint("MissingPermission")
    private void connectSelectedDevice() {
        if (devices.isEmpty()) {
            showMessage("Belum ada perangkat Bluetooth yang dipasangkan");
            return;
        }
        BluetoothDevice device = devices.get(deviceSpinner.getSelectedItemPosition());
        connectionStatus.setText("●  Menghubungkan ke " + device.getName());
        new Thread(() -> {
            try {
                if (bluetoothAdapter.isDiscovering()) bluetoothAdapter.cancelDiscovery();
                BluetoothSocket newSocket = device.createRfcommSocketToServiceRecord(SPP_UUID);
                newSocket.connect();
                socket = newSocket;
                output = socket.getOutputStream();
                handler.post(() -> connectionStatus.setTextColor(Color.rgb(100, 220, 130)));
                handler.post(() -> connectionStatus.setText("●  Tersambung: " + device.getName()));
                sendCommand("print");
                readResponses(socket.getInputStream());
            } catch (IOException error) {
                closeConnection();
                handler.post(() -> connectionStatus.setText("●  Gagal tersambung"));
                handler.post(() -> showMessage("Gagal terhubung ke ESP32"));
            }
        }).start();
    }

    private void readResponses(InputStream input) throws IOException {
        byte[] buffer = new byte[256];
        StringBuilder line = new StringBuilder();
        int count;
        while ((count = input.read(buffer)) != -1) {
            for (int index = 0; index < count; index++) {
                char character = (char) buffer[index];
                if (character == '\n' || character == '\r') {
                    if (line.length() > 0) {
                        String response = line.toString();
                        handler.post(() -> applyResponse(response));
                        line.setLength(0);
                    }
                } else {
                    line.append(character);
                }
            }
        }
        handler.post(() -> connectionStatus.setText("●  Terputus"));
    }

    private void applyResponse(String response) {
        String[] parts = response.split("#", 2);
        if (parts.length != 2) return;
        try {
            int value = Integer.parseInt(parts[1].trim());
            if (parts[0].equals("brightness")) setSeek(R.id.seekBrightness, value, R.id.tvBrightness, "%");
            if (parts[0].equals("delay")) setSeek(R.id.seekLedDelay, value, R.id.tvLedDelay, " ms");
            if (parts[0].equals("bassf")) setSeek(R.id.seekBassLow, value, R.id.tvBassLowValue, "");
            if (parts[0].equals("bassl")) setSeek(R.id.seekFullRange, value, R.id.tvFullRangeValue, "");
            if (parts[0].equals("leds")) ((EditText) findViewById(R.id.etLedCount)).setText(String.valueOf(value));
            if (parts[0].equals("pattern")) {
                if (value < 0 || value > 3) return;
                updatingPattern = true;
                patternSpinner.setSelection(value);
                updatingPattern = false;
            }
        } catch (NumberFormatException ignored) {
        }
    }

    private void setSeek(int seekId, int progress, int textId, String suffix) {
        SeekBar seek = findViewById(seekId);
        seek.setProgress(progress);
        ((TextView) findViewById(textId)).setText(progress + suffix);
    }

    private void setupControls() {
        findViewById(R.id.btnPrevious).setOnClickListener(v -> sendCommand("prev"));
        findViewById(R.id.btnNext).setOnClickListener(v -> sendCommand("next"));
        findViewById(R.id.btnPlayPause).setOnClickListener(v -> sendCommand("play"));
        findViewById(R.id.btnStop).setOnClickListener(v -> sendCommand("stop"));
        ArrayAdapter<String> patternAdapter = new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_item,
                new String[]{"Classic Bars", "Pulse Gradient", "Rainbow Comet", "Elegant Wave"});
        patternAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        patternSpinner.setAdapter(patternAdapter);
        patternSpinner.setOnItemSelectedListener(new android.widget.AdapterView.OnItemSelectedListener() {
            public void onItemSelected(android.widget.AdapterView<?> parent, View view, int position, long id) {
                if (!updatingPattern && output != null) sendCommand("pattern#" + position);
            }
            public void onNothingSelected(android.widget.AdapterView<?> parent) {}
        });
        bindSeek(R.id.seekBassLow, R.id.tvBassLowValue, "bassf", "");
        bindSeek(R.id.seekFullRange, R.id.tvFullRangeValue, "bassl", "");
        bindSeek(R.id.seekBrightness, R.id.tvBrightness, "brightness", "%");
        bindSeek(R.id.seekLedDelay, R.id.tvLedDelay, "delay", " ms", 1);
        findViewById(R.id.btnSaveVisualizer).setOnClickListener(v -> saveVisualizer());
        findViewById(R.id.btnSaveSettings).setOnClickListener(v -> sendCommand("name#" + text(R.id.etBluetoothName)));
    }

    private void bindSeek(int seekId, int textId, String command, String suffix) { bindSeek(seekId, textId, command, suffix, 0); }
    private void bindSeek(int seekId, int textId, String command, String suffix, int offset) {
        SeekBar seek = findViewById(seekId);
        seek.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(SeekBar bar, int progress, boolean fromUser) {
                ((TextView) findViewById(textId)).setText((progress + offset) + suffix);
                if (fromUser) sendCommand(command + "#" + (progress + offset));
            }
            public void onStartTrackingTouch(SeekBar bar) {}
            public void onStopTrackingTouch(SeekBar bar) {}
        });
    }

    private String text(int id) { return ((EditText) findViewById(id)).getText().toString().trim(); }

    private void saveVisualizer() {
        sendCommand("leds#" + text(R.id.etLedCount));
        sendCommand("pattern#" + patternSpinner.getSelectedItemPosition());
        sendCommand("save");
        showMessage("Pengaturan visualizer disimpan");
    }

    private synchronized void sendCommand(String command) {
        if (output == null) { showMessage("Hubungkan ESP32 terlebih dahulu"); return; }
        try { output.write((command + "\n").getBytes()); output.flush(); }
        catch (IOException error) { showMessage("Koneksi Bluetooth terputus"); }
    }

    private void closeConnection() {
        try { if (socket != null) socket.close(); } catch (IOException ignored) {}
        socket = null; output = null;
    }

    private void showMessage(String message) { Toast.makeText(this, message, Toast.LENGTH_SHORT).show(); }

    @Override protected void onDestroy() { closeConnection(); super.onDestroy(); }
}