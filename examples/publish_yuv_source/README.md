### Generate yuv nv12 stream with gstreamer

```bash
gst-launch-1.0 avfvideosrc device-index=0 ! videoconvert ! videorate ! videoscale ! video/x-raw,format=NV12,width=1280,height=720,framerate=30/1 ! queue ! tcpserversink host=0.0.0.0 port=5004 sync=false
```

### Publish stream to LiveKit

```bash
./build-release/bin/PublishYuvSource --url wss://... --token <JWT> --tcp 0.0.0.0:5004 --raw-width 1280 --raw-height 720 --raw-fps 30
```
