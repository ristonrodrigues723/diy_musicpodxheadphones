//this code is taken from github just made my pin allocatio + minor buton changes original code didnt allow some functions examples are found on github repos and yt vids
// code source-https://github.com/schreibfaul1/ESP32-audioI2S
#include "Arduino.h"
#include "WiFi.h"         // For internet acess if needs
#include "Audio.h"        // The ESP32-audioI2S library
#include "SD.h"           // For SD card access
#include "FS.h"           // For file system access
#include <vector>         // For dynamic song list
#include <Adafruit_GFX.h> // Core graphics library
#include <Adafruit_ST7735.h> // ST7735 TFT library

// I2S Audio Pins (from U4 UDA1334A(this iss the dac module if ur in us ull need to use ur dac poins) )
#define I2S_DOUT      31
#define I2S_BCLK      40
#define I2S_LRC       41

// SD Card Pins (from U9 SD CARD READER in pcb the spi ardon reader)
#define SD_CS         35
#define SPI_MOSI      36 // Shared with TFT_SDA
#define SPI_MISO      38
#define SPI_SCK       37 // Shared with TFT_SCK

// TFT Display Pins (from D1 1.8 INCH TFT DISPLAY ST7735 from robocraze that im planning to buy)
#define TFT_CS        4
#define TFT_DC        6
#define TFT_RST       5

// Button Pins (from U10, U11, U12, U13, U14 buttons)
#define BUTTON_CENTER 15 // center btn
#define BUTTON_UP     16 // up btn
#define BUTTON_DOWN   17 // dn btn
#define BUTTON_RIGHT  18 // right btn
#define BUTTON_LEFT   19 // left btn

// --- Global Objects ---
Audio audio;
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, SPI_MOSI, SPI_SCK, TFT_RST);

// --- Button Handling Variables ---
const int BUTTON_PINS[] = {BUTTON_CENTER, BUTTON_LEFT, BUTTON_RIGHT, BUTTON_UP, BUTTON_DOWN};
const int NUM_BUTTONS = sizeof(BUTTON_PINS) / sizeof(BUTTON_PINS[0]);

unsigned long lastDebounceTime[NUM_BUTTONS] = {0}; //check Last time whn button state changed
int buttonState[NUM_BUTTONS] = {HIGH};
int lastButtonState[NUM_BUTTONS] = {HIGH};

// Removed pressStartTime, isLongPress, clickCount, lastClickTime
// since we are simplifying button logic to only single presses initially.

const unsigned long DEBOUNCE_DELAY = 50;
// Removed DOUBLE_CLICK_TIME and LONG_PRESS_TIME

// --- Playback and Menu State Variables ---
enum PlaybackState {
    PAUSED,
    PLAYING
};
PlaybackState currentPlaybackState = PAUSED;

enum MenuState {
    MAIN_PLAYBACK_SCREEN,
    PLAYLIST_BROWSE_SCREEN
};
MenuState currentMenuState = MAIN_PLAYBACK_SCREEN;

String currentSongPath = "";
int currentSongIndex = -1;
std::vector<String> songList; //song paTHS on card are stored here
int menuSelectedItem = 0;


int currentVolume = 12; //default vol

void scanMusicFiles();
void playSong(String path);
void adjustVolume(int newVolume);
void updateTFTDisplay();
void handleButtons();
void processButtonEvent(int buttonIndex); // Simplified: no doubleClick/longPress args

// Audio Callback Functions (ESP32-audioI2S example on the https://github.com/schreibfaul1/ESP32-audioI2S)
void audio_info(const char *info);
void audio_id3data(const char *info);
void audio_eof_mp3(const char *info);
void audio_showstation(const char *info);
void audio_showstreamtitle(const char *info);
void audio_bitrate(const char *info);
void audio_commercial(const char *info);
void audio_icyurl(const char *info);
void audio_lasthost(const char *info);
void audio_eof_speech(const char *info);

// --- Setup Function ---
void setup() {
    Serial.begin(115200);
    Serial.println("Starting ESP32 Audio Player...");

    // Initializes SPI for SD card and TFT
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);

    // Initialize SD card
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH); // SD card CS hih
 //when not neing used
  if (!SD.begin(SD_CS)) {
        Serial.println("SD Card Mount Failed");
        tft.fillScreen(ST7735_BLACK);
        tft.setCursor(0, 0);
        tft.setTextColor(ST7735_RED);
        tft.println("SD ERROR!");
        delay(3000);
        // lop for re apen,to
    } else {
        Serial.println("SD Card Initialized");
    }

    // Initializes TFT
    tft.initR(INITR_18GREENTAB);
    tft.setRotation(1);
    tft.fillScreen(ST7735_BLACK);
    tft.setTextWrap(false);
    tft.setTextSize(1);
    tft.setTextColor(ST7735_WHITE);
    tft.setCursor(0, 0);
    tft.println("Initializing Audio...");


    for (int i = 0; i < NUM_BUTTONS; i++) {
        pinMode(BUTTON_PINS[i], INPUT_PULLUP);
    }


    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(currentVolume);

    // Scan for music files on SD card
    scanMusicFiles();

    updateTFTDisplay(); // Initial display
}

// --- Main Loop ---
void loop() {
    audio.loop();      // Keep the audio library running
    handleButtons();    // Check for button presses and execute actions
    vTaskDelay(1);      // Yield to other tasks
}

// --- Helper Functions ---

void scanMusicFiles() {
    songList.clear(); // Clear existing list
    Serial.println("Scanning SD card for audio files...");

    File root = SD.open("/");
    if (!root) {
        Serial.println("Failed to open root directory.");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String fileName = file.name();
            // Basic check for common audio file extensions
            if (fileName.endsWith(".mp3") || fileName.endsWith(".wav") ||
                fileName.endsWith(".flac") || fileName.endsWith(".m4a") ||
                fileName.endsWith(".ogg") || fileName.endsWith(".opus") ||
                fileName.endsWith(".aac")) {
                songList.push_back("/" + fileName); // Store full path
            }
        }
        file = root.openNextFile();
    }
    root.close();

    if (songList.empty()) {
        Serial.println("No supported audio files found on SD card.");
    } else {
        Serial.print("Found ");
        Serial.print(songList.size());
        Serial.println(" audio files.");
    }
}

void playSong(String path) {
    if (path == currentSongPath && currentPlaybackState == PLAYING) {
        // Already playing this song, do nothing
        return;
    }
    Serial.print("Playing: ");
    Serial.println(path);
    audio.stop(); // Stop current playback if any
    audio.connecttoFS(SD, path.c_str());
    currentSongPath = path;
    currentPlaybackState = PLAYING;
    updateTFTDisplay(); // Update display immediately
}

void adjustVolume(int newVolume) {
    if (newVolume < 0) newVolume = 0;
    if (newVolume > 21) newVolume = 21; // Max volume  ony alow for library
    currentVolume = newVolume;
    audio.setVolume(currentVolume);
    Serial.print("Volume set to: ");
    Serial.println(currentVolume);
    updateTFTDisplay(); // Updat display
}

// This function manages the different display states
void updateTFTDisplay() {
    tft.fillScreen(ST7735_BLACK); // Clears te screen

    switch (currentMenuState) {
        case MAIN_PLAYBACK_SCREEN: {
            tft.setCursor(0, 0);
            tft.setTextColor(ST7735_CYAN);
            tft.println("Now Playing:");

            tft.setTextColor(ST7735_WHITE);
            String displaySongName = "No song selected.";
            if (currentSongPath != "") {
                int lastSlash = currentSongPath.lastIndexOf('/');
                displaySongName = currentSongPath.substring(lastSlash + 1);
            }
            // Display song name, potentially truncate if too long
            if (displaySongName.length() > 20) { // Adjust based on your font size and screen width
                displaySongName = displaySongName.substring(0, 17) + "...";
            }
            tft.setCursor(0, 16);
            tft.println(displaySongName);

            tft.setCursor(0, 40);
            tft.setTextColor(ST7735_GREEN);
            tft.print("Volume: ");
            tft.println(currentVolume);

            tft.setCursor(0, 56);
            tft.setTextColor(ST7735_YELLOW);
            tft.print("Status: ");
            if (currentPlaybackState == PLAYING) {
                tft.println("Playing");
            } else {
                tft.println("Paused");
            }
            tft.setCursor(0, 80);
            tft.setTextColor(ST7735_MAGENTA);
         tft.println("C: Play/Pause/List");
               tft.println("R/L: Next/Prev");
             tft.println("U/D: Vol +/-");
            tft.println(""); 
            break;
        }

           case PLAYLIST_BROWSE_SCREEN: {
            tft.setCursor(0, 0);
            tft.setTextColor(ST7735_CYAN);
            tft.println("Playlist:");
            int maxItemsOnScreen = (tft.height() - 20) / 10;
            int displayStartIdx = max(0, menuSelectedItem - maxItemsOnScreen / 2);
            int displayEndIdx = min((int)songList.size() - 1, displayStartIdx + maxItemsOnScreen - 1);

            if (displayEndIdx - displayStartIdx + 1 < maxItemsOnScreen) {
                displayStartIdx = max(0, displayEndIdx - maxItemsOnScreen + 1);
            }

            for (int i = displayStartIdx; i <= displayEndIdx; ++i) {
                String displayName = songList[i];
                int lastSlash = displayName.lastIndexOf('/');
                displayName = displayName.substring(lastSlash + 1);

  
                if (displayName.length() > 20) {
                    displayName = displayName.substring(0, 17) + "...";
                }

                tft.setCursor(0, 16 + (i - displayStartIdx) * 10);
                if (i == menuSelectedItem) {
                    tft.setTextColor(ST7735_YELLOW); 
                    tft.print("> "); 
                } else {
                    tft.setTextColor(ST7735_WHITE);
                    tft.print("  "); 
                }
                tft.println(displayName);
            }

            // If playlist is empty
         if (songList.empty()) {
                tft.setTextColor(ST7735_RED);
                      tft.setCursor(0, 16);
                tft.println("No songs found!");
            }


            tft.setCursor(0, tft.height() - 20)
            tft.setTextColor(ST7735_MAGENTA);
           tft.println("C: Select/Back"); 
       tft.println("U/D: Scroll");
               break;
        }
    }
}

void handleButtons() {
    unsigned long currentTime = millis();

    for (int i = 0; i < NUM_BUTTONS; i++) {
        int reading = digitalRead(BUTTON_PINS[i]);


        if (reading != lastButtonState[i]) {
            lastDebounceTime[i] = currentTime;
        }

      if ((currentTime - lastDebounceTime[i]) > DEBOUNCE_DELAY) {
            if (reading != buttonState[i]) {
                buttonState[i] = reading;

                if (buttonState[i] == LOW) { 
                    processButtonEvent(i); /
                }
            }
        }
        lastButtonState[i] = reading; // Update s last stae
    }

}

// --- Simplified processButtonEvent ---
void processButtonEvent(int buttonIndex) {
    Serial.print("Button ");
    Serial.print(BUTTON_PINS[buttonIndex]);
    Serial.print(" (Index: "); Serial.print(buttonIndex); Serial.print(")");
    Serial.println(" Pressed!"); 

    switch (currentMenuState) {
        case MAIN_PLAYBACK_SCREEN:
            if (buttonIndex == 0) { // Center Button
               
                if (currentPlaybackState == PLAYING || currentSongPath != "") { 
                    audio.pauseResume();
                    currentPlaybackState = (currentPlaybackState == PLAYING) ? PAUSED : PLAYING;
                    Serial.print("Playback: "); Serial.println(currentPlaybackState == PLAYING ? "Playing" : "Paused");
                } else { 
                    currentMenuState = PLAYLIST_BROWSE_SCREEN;
                    menuSelectedItem = currentSongIndex; // Start playlist view at current song
                    Serial.println("Center Button: To Playlist Browse");
                }
            } else if (buttonIndex == 1) { // Left Button (Previous song go t )
                if (!songList.empty()) {
                    currentSongIndex = (currentSongIndex - 1 + songList.size()) % songList.size();
                    playSong(songList[currentSongIndex]);
                }
            } else if (buttonIndex == 2) { // Right Button (Next song plays)
                if (!songList.empty()) {
                    currentSongIndex = (currentSongIndex + 1) % songList.size();
                    playSong(songList[currentSongIndex]);
                }
            } else if (buttonIndex == 3) { // Up Button (inc vol- Up)
                adjustVolume(currentVolume + 1); // Simple +1 volume
            } else if (buttonIndex == 4) { // Down Button (decreases Volume)
                adjustVolume(currentVolume - 1); 
            }
            break;

        case PLAYLIST_BROWSE_SCREEN:
            if (buttonIndex == 0) { // Center Button
          
                if (!songList.empty() && menuSelectedItem >= 0 && menuSelectedItem < songList.size()) {
                    currentSongIndex = menuSelectedItem;
                    playSong(songList[currentSongIndex]);
                    currentMenuState = MAIN_PLAYBACK_SCREEN; 
                    Serial.print("Center Button: Play song index "); Serial.println(currentSongIndex);
            } else {
                    currentMenuState = MAIN_PLAYBACK_SCREEN;
                    Serial.println("Center Button: Back to Main Playback (no selection or empty list)");
                }
          } else if (buttonIndex == 1) {
                currentMenuState = MAIN_PLAYBACK_SCREEN;
                Serial.println("Left Button: Back to Main Playback");
        } else if (buttonIndex == 2) { // Right Button

                Serial.println("Right Button: No action in Playlist Browse (currently)");
            } else if (buttonIndex == 3) { // Up Button (Scrolls up)
                if (menuSelectedItem > 0) {
                    menuSelectedItem--;
                    Serial.print("Playlist: Selected index "); Serial.println(menuSelectedItem);
                }
            } else if (buttonIndex == 4) { 
                if (menuSelectedItem < songList.size() - 1) {
                    menuSelectedItem++;
                    Serial.print("Playlist: Selected index "); Serial.println(menuSelectedItem);
                }
            }
            break;


    }
    updateTFTDisplay(); 
}

// --- Audio Callback Functions  ---
void audio_info(const char *info) {
    Serial.print("info        ");
    Serial.println(info);

}

void audio_id3data(const char *info) { //deqals with metadata
    Serial.print("id3data     ");
    Serial.println(info);
    // Update song info on scren
}

void audio_eof_mp3(const char *info) {
    Serial.print("eof_mp3     ");
    Serial.println(info);
    // Auto-pplays next song if its there in sd card
    if (currentSongIndex != -1 && !songList.empty()) {
        currentSongIndex = (currentSongIndex + 1) % songList.size();
        playSong(songList[currentSongIndex]);
    } else {
        currentPlaybackState = PAUSED; 
        updateTFTDisplay();
    }
}

void audio_showstation(const char *info) {
    Serial.print("station     ");
    Serial.println(info);
}

void audio_showstreamtitle(const char *info) {
    Serial.print("streamtitle ");
    Serial.println(info);
}

void audio_bitrate(const char *info) {
    Serial.print("bitrate     ");
    Serial.println(info);
}

void audio_commercial(const char *info) { 
    Serial.print("commercial  ");
    Serial.println(info);
}

void audio_icyurl(const char *info) { //homepage will take me to it
    Serial.print("icyurl      ");
    Serial.println(info);
}

void audio_lasthost(const char *info) { 
    Serial.print("lasthost    ");
    Serial.println(info);
}

void audio_eof_speech(const char *info) {
    Serial.print("eof_speech  ");
    Serial.println(info);
}
