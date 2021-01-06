# ESP8266-SmartMeter-Exporter

Some Smartmeters expose their current reading through an infrared LED and send it as serial data, either in a regular interval or per request.
This project makes an ESP8266 understand this data and provides it in a format, that can be imported by [Prometheus](https://prometheus.io/) monitoring software.

To interface the ESP with the Smartmeter, you need some hardware to receive and (if needed) send infrared signals. See e.g. [here](https://wiki.volkszaehler.org/hardware/controllers/ir-schreib-lesekopf) (german site) for some details about this.
The writing/reading head should then be connected to some pins on the ESP that act as a SoftwareSerial interface (see [Configuration](#Configuration)).

## Compatibility

This code is currently only compatible with the `EasyMeter Q3D` model of a german manufacturer. This Smartmeter uses a OBIS-based text format to transmit it's data in a 2 second interval. Requesting data using an infrared LED on the ESP side is not required for this model, but I added support for this to my code anyway.

I'm open for pull requests that extend the support to other models. Also support for the more broadly used SML protocol should be fairly easy to add.

## Configuration

Before compiling this repository, please create a `src/config.h` file where your configuration lives. You can find a template for this in `src/config.sample.h`.

## Usage

To see which metrics are exposed, please point your browser to `http://<device name or IP>/metrics`. You can add this URL to your Prometheus instance.

## OTA-Update

When your ESP is flashed and properly installed at it's final location, it's useful to flash updates over the air. To do this, either visit `http://<device name or IP>/update` or use the following command to upload a new firmware:

```
curl -u USER:PASSWORD -F "image=@.pio/build/esp12e/firmware.bin" http://<device name or IP>/update
```

**Have fun!**
