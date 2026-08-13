#pragma once

class VoiceAudioQueueLifecycle {
public:
    bool needsBufferScheduling() const
    {
        return !buffers_scheduled_;
    }

    void markBuffersScheduled()
    {
        buffers_scheduled_ = true;
    }

    void markStopped()
    {
        buffers_scheduled_ = false;
    }

private:
    bool buffers_scheduled_ = false;
};
