// Arduino IDE build bridge for the shared ESP32 video receiver.
//
// Arduino's sketch builder automatically compiles .cpp files located in the
// product sketch directory, but it does not automatically compile arbitrary
// .cpp files from ../../core. Keep the implementation in the shared core and
// include it here so the product entry links the same implementation.
#include "../../core/video/spi_video_receiver.cpp"
