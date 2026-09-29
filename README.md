# Gleem Imager

Gleem Imager writes the [Gleem IRL Sidekick](https://gleem.gg/irl-sidekick) image to an SD card for
the Orange Pi 5 Plus, Raspberry Pi 4 and Raspberry Pi 5. Pick your board, pick
the image, pick the card: it downloads the image from `get.gleem.gg`, writes it
and verifies it.

Downloads for Windows and Linux (AppImage) are on
[get.gleem.gg/imager](https://get.gleem.gg/imager/).

## Based on Raspberry Pi Imager

Gleem Imager is a modified version of
[Raspberry Pi Imager](https://github.com/raspberrypi/rpi-imager) by Raspberry Pi
Ltd, used under the Apache License 2.0 (see [license.txt](license.txt) and
[NOTICE](NOTICE)). It is **not affiliated with, endorsed by or supported by
Raspberry Pi Ltd**. "Raspberry Pi" is a trademark of Raspberry Pi Ltd.

What differs from upstream:

- Name, icons and colours are Gleem's; the settings, the `gleem-imager://` link
  handler and the `.gleem-imager-manifest` file type are its own, so it can be
  installed next to Raspberry Pi Imager without either affecting the other.
- The image list comes from `https://get.gleem.gg/imager/os_list.json`
  ([gleem/os_list.json](gleem/os_list.json)).
- No telemetry: the download counter is off and has no address to send to.
- Raspberry Pi Connect and the options for it are hidden. OS customisation only
  appears for images that ask for it, and Gleem's images do not.
- The Windows installer has its own AppId and never touches a Raspberry Pi
  Imager installation.
- The version comes only from `gleem-vX.Y.Z` tags.

The upstream README is in [README.upstream.md](README.upstream.md); building
works the same way (see [BUILDING.md](BUILDING.md)), and the binary is called
`gleem-imager`.

## Updating from upstream

`main` tracks `raspberrypi/rpi-imager`; the Gleem changes live on `gleem`.
Merge `main` into `gleem` to pick up upstream fixes.

## Releasing

1. Tag `gleem-vX.Y.Z` on `gleem` and push the tag. CI builds the Windows
   installer (unsigned) and the Linux AppImage.
2. With the Certum code-signing card plugged in (and proCertumCardManager
   installed in /opt), run `gleem/release.sh gleem-vX.Y.Z` and enter the
   card's PIN when asked.
   It signs the installer, verifies it and publishes both to
   `get.gleem.gg/imager/vX.Y.Z/`, moving `imager/latest` last.
3. Raise `imager.latest_version` in `gleem/os_list.json` so running copies
   offer the update; pushing that publishes the list.
