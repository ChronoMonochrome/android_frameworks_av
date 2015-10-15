/*
 * Copyright (C) 2012 The Android Open Source Project
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

#ifndef GENERIC_SOURCE_H_

#define GENERIC_SOURCE_H_

#include "NuPlayer.h"
#include "NuPlayerSource.h"

#include "ATSParser.h"

#include <media/mediaplayer.h>

namespace android {

struct AnotherPacketSource;
struct ARTSPController;
struct DataSource;
struct MediaSource;
class MediaBuffer;

struct NuPlayer::GenericSource : public NuPlayer::Source {
    GenericSource(
            const sp<AMessage> &notify,
            const sp<IMediaHTTPService> &httpService,
            const char *url,
            const KeyedVector<String8, String8> *headers,
            bool isWidevine = false,
            bool uidValid = false,
            uid_t uid = 0);

    GenericSource(
            const sp<AMessage> &notify,
            int fd, int64_t offset, int64_t length);

    virtual void prepareAsync();

    virtual void start();

    virtual status_t feedMoreTSData();

    virtual status_t dequeueAccessUnit(bool audio, sp<ABuffer> *accessUnit);

    virtual status_t getDuration(int64_t *durationUs);
    virtual size_t getTrackCount() const;
    virtual sp<AMessage> getTrackInfo(size_t trackIndex) const;
    virtual status_t selectTrack(size_t trackIndex, bool select);
    virtual status_t seekTo(int64_t seekTimeUs);

    virtual status_t setBuffers(bool audio, Vector<MediaBuffer *> &buffers);

protected:
    virtual ~GenericSource();

    virtual void onMessageReceived(const sp<AMessage> &msg);

    virtual sp<MetaData> getFormatMeta(bool audio);

private:
    enum {
        kWhatFetchSubtitleData,
        kWhatSendSubtitleData,
        kWhatChangeAVSource,
    };

    Vector<sp<MediaSource> > mSources;

    struct Track {
        size_t mIndex;
        sp<MediaSource> mSource;
        sp<AnotherPacketSource> mPackets;
    };

    Track mAudioTrack;
    Track mVideoTrack;
    Track mSubtitleTrack;

    int32_t mFetchSubtitleDataGeneration;
    int64_t mDurationUs;
    bool mAudioIsVorbis;
    bool mIsWidevine;
    bool mUIDValid;
    uid_t mUID;

    void initFromDataSource(const sp<DataSource> &dataSource);

    sp<ABuffer> mediaBufferToABuffer(
            MediaBuffer *mbuf,
            media_track_type trackType,
            int64_t *actualTimeUs = NULL);

    void readBuffer(
            media_track_type trackType,
            int64_t seekTimeUs = -1ll, int64_t *actualTimeUs = NULL, bool formatChange = false);

    DISALLOW_EVIL_CONSTRUCTORS(GenericSource);
};

}  // namespace android

#endif  // GENERIC_SOURCE_H_
