#include <M5Cardputer.h>

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);

  // Set Up the Display
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.setTextColor(TFT_GREEN);
  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.setCursor(8, 8);

  M5Cardputer.Display.println("BERTIE");

  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(TFT_YELLOW);
  M5Cardputer.Display.println("Bindery Executive for Robotics, Tools, Information & Equipment");
  M5Cardputer.Display.setTextColor(TFT_WHITE);
  M5Cardputer.Display.println();
  M5Cardputer.Display.println("Terminal v0.1");
  M5Cardputer.Display.println("Keyboard test:");
}

void loop() {
    M5Cardputer.update();

    //Read keyboard input
    if (M5Cardputer.Keyboard.isChange() &&
        M5Cardputer.Keyboard.isPressed()) {

            auto keys = M5Cardputer.Keyboard.keysState();
        
            for (char c : keys.word) {
                M5Cardputer.Display.print(c);
            }

            if (keys.enter) {
                M5Cardputer.Display.println();
            }
        }   

        delay(10);
}
