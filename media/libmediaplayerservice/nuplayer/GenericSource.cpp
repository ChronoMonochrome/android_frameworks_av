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

#include "GenericSource.h"

#include "AnotherPacketSource.h"

#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/FileSource.h>
#include <media/stagefright/MediaBuffer.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaExtractor.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MetaData.h>
#include "../../libstagefright/include/WVMExtractor.h"

namespace android {

NuPlayer::GenericSource::GenericSource(
        const sp<AMessage> &notify,
        const sp<IMediaHTTPService> &httpService,
        const char *url,
        const KeyedVector<String8, String8> *headers,
        bool isWidevine,
        bool uidValid,
        uid_t uid)
    : Source(notify),
      mFetchSubtitleDataGeneration(0),
      mDurationUs(0ll),
      mAudioIsVorbis(false),
      mIsWidevine(isWidevine),
      mUIDValid(uidValid),
      mUID(uid) {
    DataSource::RegisterDefaultSniffers();

    sp<DataSource> dataSource =
        DataSource::CreateFromURI(httpService, url, headers);
    CHECK(dataSource != NULL);

    initFromDataSource(dataSource);
}

NuPlayer::GenericSource::GenericSource(
        const sp<AMessage> &notify,
        int fd, int64_t offset, int64_t length)
    : Source(notify),
      mFetchSubtitleDataGeneration(0),
      mDurationUs(0ll),
      mAudioIsVorbis(false) {
    DataSource::RegisterDefaultSniffers();

    sp<DataSource> dataSource = new FileSource(dup(fd), offset, length);

    initFromDataSource(dataSource);
}

void NuPlayer::GenericSource::initFromDataSource(
        const sp<DataSource> &dataSource) {
    sp<MediaExtractor> extractor;

    if (mIsWidevine) {
        String8 mimeType;
        float confidence;
        sp<AMessage> dummy;
        bool success;

        success = SniffWVM(dataSource, &mimeType, &confidence, &dummy);
        if (!success
                || strcasecmp(
                    mimeType.string(), MEDIA_MIMETYPE_CONTAINER_WVM)) {
            ALOGE("unsupported widevine mime: %s", mimeType.string());
            return;
        }

        sp<WVMExtractor> wvmExtractor = new WVMExtractor(dataSource);
        wvmExtractor->setAdaptiveStreamingMode(true);
        if (mUIDValid) {
            wvmExtractor->setUID(mUID);
        }
        extractor = wvmExtractor;
    } else {
        extractor = MediaExtractor::Create(dataSource);
    }

    CHECK(extractor != NULL);

    sp<MetaData> fileMeta = extractor->getMetaData();
    if (fileMeta != NULL) {
        int64_t duration;
        if (fileMeta->findInt64(kKeyDuration, &duration)) {
            mDurationUs = duration;
        }
    }

    for (size_t i = 0; i < extractor->countTracks(); ++i) {
        sp<MetaData> meta = extractor->getTrackMetaData(i);

        const char *mime;
        CHECK(meta->findCString(kKeyMIMEType, &mime));

        sp<MediaSource> track = extractor->getTrack(i);

        if (!strncasecmp(mime, "audio/", 6)) {
            if (mAudioTrack.mSource == NULL) {
                mAudioTrack.mIndex = i;
                mAudioTrack.mSource = track;

                if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_VORBIS)) {
                    mAudioIsVorbis = true;
                } else {
                    mAudioIsVorbis = false;
                }
            }
        } else if (!strncasecmp(mime, "video/", 6)) {
            if (mVideoTrack.mSource == NULL) {
                mVideoTrack.mIndex = i;
                mVideoTrack.mSource = track;
            }
        }

        if (track != NULL) {
            CHECK_EQ(track->start(), (status_t)OK);
            mSources.push(track);
            int64_t durationUs;
            if (meta->findInt64(kKeyDuration, &durationUs)) {
                if (durationUs > mDurationUs) {
                    mDurationUs = durationUs;
                }
            }
        }
    }
}

status_t NuPlayer::GenericSource::setBuffers(bool audio, Vector<MediaBuffer *> &buffers) {
    if (mIsWidevine && !audio) {
        return mVideoTrack.mSource->setBuffers(buffers);
    }
    return INVALID_OPERATION;
}

NuPlayer::GenericSource::~GenericSource() {
}

void NuPlayer::GenericSource::prepareAsync() {
    if (mVideoTrack.mSource != NULL) {
        sp<MetaData> meta = mVideoTrack.mSource->getFormat();

        int32_t width, height;
        CHECK(meta->findInt32(kKeyWidth, &width));
        CHECK(meta->findInt32(kKeyHeight, &height));

        notifyVideoSizeChanged(width, height);
    }

    notifyFlagsChanged(
            (mIsWidevine ? FLAG_SECURE : 0)
            | FLAG_CAN_PAUSE
            | FLAG_CAN_SEEK_BACKWARD
            | FLAG_CAN_SEEK_FORWARD
            | FLAG_CAN_SEEK);

    notifyPrepared();
}

void NuPlayer::GenericSource::start() {
    ALOGI("start");

    if (mAudioTrack.mSource != NULL) {
        mAudioTrack.mPackets =
            new AnotherPacketSource(mAudioTrack.mSource->getFormat());

        readBuffer(MEDIA_TRACK_TYPE_AUDIO);
    }

    if (mVideoTrack.mSource != NULL) {
        mVideoTrack.mPackets =
            new AnotherPacketSource(mVideoTrack.mSource->getFormat());

        readBuffer(MEDIA_TRACK_TYPE_VIDEO);
    }
}

status_t NuPlayer::GenericSource::feedMoreTSData() {
    return OK;
}

void NuPlayer::GenericSource::onMessageReceived(const sp<AMessage> &msg) {
    switch (msg->what()) {
      case kWhatFetchSubtitleData:
      {
          int32_t generation;
          CHECK(msg->findInt32("generation", &generation));
          if (generation != mFetchSubtitleDataGeneration) {
              // stale
              break;
          }

          int32_t avail;
          if (mSubtitleTrack.mPackets->hasBufferAvailable(&avail)) {
              break;
          }

          int64_t timeUs;
          CHECK(msg->findInt64("timeUs", &timeUs));

          int64_t subTimeUs;
          readBuffer(MEDIA_TRACK_TYPE_SUBTITLE, timeUs, &subTimeUs);

          const int64_t oneSecUs = 1000000ll;
          const int64_t delayUs = subTimeUs - timeUs - oneSecUs;
          sp<AMessage> msg2 = new AMessage(kWhatSendSubtitleData, id());
          msg2->setInt32("generation", generation);
          msg2->post(delayUs < 0 ? 0 : delayUs);
          ALOGV("kWhatFetchSubtitleData generation %d, delayUs %lld",
                  mFetchSubtitleDataGeneration, delayUs);

          break;
      }

      case kWhatSendSubtitleData:
      {
          int32_t generation;
          CHECK(msg->findInt32("generation", &generation));
          if (generation != mFetchSubtitleDataGeneration) {
              // stale
              break;
          }

          int64_t subTimeUs;
          if (mSubtitleTrack.mPackets->nextBufferTime(&subTimeUs) != OK) {
              break;
          }

          int64_t nextSubTimeUs;
          readBuffer(MEDIA_TRACK_TYPE_SUBTITLE, -1, &nextSubTimeUs);

          sp<ABuffer> buffer;
          status_t dequeueStatus = mSubtitleTrack.mPackets->dequeueAccessUnit(&buffer);
          if (dequeueStatus != OK) {
              ALOGE("kWhatSendSubtitleData dequeueAccessUnit: %d", dequeueStatus);
          } else {
              sp<AMessage> notify = dupNotify();
              notify->setInt32("what", kWhatSubtitleData);
              notify->setBuffer("buffer", buffer);
              notify->post();

              const int64_t delayUs = nextSubTimeUs - subTimeUs;
              msg->post(delayUs < 0 ? 0 : delayUs);
          }

          break;
      }

      case kWhatChangeAVSource:
      {
          int32_t trackIndex;
          CHECK(msg->findInt32("trackIndex", &trackIndex));
          const sp<MediaSource> source = mSources.itemAt(trackIndex);

          Track* track;
          const char *mime;
          media_track_type trackType, counterpartType;
          sp<MetaData> meta = source->getFormat();
          meta->findCString(kKeyMIMEType, &mime);
          if (!strncasecmp(mime, "audio/", 6)) {
              track = &mAudioTrack;
              trackType = MEDIA_TRACK_TYPE_AUDIO;
              counterpartType = MEDIA_TRACK_TYPE_VIDEO;;
          } else {
              CHECK(!strncasecmp(mime, "video/", 6));
              track = &mVideoTrack;
              trackType = MEDIA_TRACK_TYPE_VIDEO;
              counterpartType = MEDIA_TRACK_TYPE_AUDIO;;
          }


          track->mSource = source;
          track->mIndex = trackIndex;

          status_t avail;
          if (!track->mPackets->hasBufferAvailable(&avail)) {
              // sync from other source
              TRESPASS();
              break;
          }

          int64_t timeUs, actualTimeUs;
          const bool formatChange = true;
          sp<AMessage> latestMeta = track->mPackets->getLatestMeta();
          CHECK(latestMeta != NULL && latestMeta->findInt64("timeUs", &timeUs));
          readBuffer(trackType, timeUs, &actualTimeUs, formatChange);
          readBuffer(counterpartType, -1, NULL, formatChange);
          ALOGV("timeUs %lld actualTimeUs %lld", timeUs, actualTimeUs);

          break;
      }

      default:
          Source::onMessageReceived(msg);
          break;
    }
}

sp<MetaData> NuPlayer::GenericSource::getFormatMeta(bool audio) {
    sp<MediaSource> source = audio ? mAudioTrack.mSource : mVideoTrack.mSource;

    if (source == NULL) {
        return NULL;
    }

    return source->getFormat();
}

status_t NuPlayer::GenericSource::dequeueAccessUnit(
        bool audio, sp<ABuffer> *accessUnit) {
    Track *track = audio ? &mAudioTrack : &mVideoTrack;

    if (track->mSource == NULL) {
        return -EWOULDBLOCK;
    }

    if (mIsWidevine && !audio) {
        // try to read a buffer as we may not have been able to the last time
        readBuffer(MEDIA_TRACK_TYPE_AUDIO, -1ll);
    }

    status_t finalResult;
    if (!track->mPackets->hasBufferAvailable(&finalResult)) {
        return (finalResult == OK ? -EWOULDBLOCK : finalResult);
    }

    status_t result = track->mPackets->dequeueAccessUnit(accessUnit);

    if (!track->mPackets->hasBufferAvailable(&finalResult)) {
        readBuffer(audio? MEDIA_TRACK_TYPE_AUDIO : MEDIA_TRACK_TYPE_VIDEO, -1ll);
    }

    if (mSubtitleTrack.mSource == NULL) {
        return result;
    }

    CHECK(mSubtitleTrack.mPackets != NULL);
    if (result != OK) {
        mSubtitleTrack.mPackets->clear();
        mFetchSubtitleDataGeneration++;
        return result;
    }

    int64_t timeUs;
    status_t eosResult; // ignored
    CHECK((*accessUnit)->meta()->findInt64("timeUs", &timeUs));
    if (!mSubtitleTrack.mPackets->hasBufferAvailable(&eosResult)) {
        sp<AMessage> msg = new AMessage(kWhatFetchSubtitleData, id());
        msg->setInt64("timeUs", timeUs);
        msg->setInt32("generation", mFetchSubtitleDataGeneration);
        msg->post();
    }

    return result;
}

status_t NuPlayer::GenericSource::getDuration(int64_t *durationUs) {
    *durationUs = mDurationUs;
    return OK;
}

size_t NuPlayer::GenericSource::getTrackCount() const {
    return mSources.size();
}

sp<AMessage> NuPlayer::GenericSource::getTrackInfo(size_t trackIndex) const {
    size_t trackCount = mSources.size();
    if (trackIndex >= trackCount) {
        return NULL;
    }

    sp<AMessage> format = new AMessage();
    sp<MetaData> meta = mSources.itemAt(trackIndex)->getFormat();

    const char *mime;
    CHECK(meta->findCString(kKeyMIMEType, &mime));

    int32_t trackType;
    if (!strncasecmp(mime, "video/", 6)) {
        trackType = MEDIA_TRACK_TYPE_VIDEO;
    } else if (!strncasecmp(mime, "audio/", 6)) {
        trackType = MEDIA_TRACK_TYPE_AUDIO;
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_TEXT_3GPP)) {
        trackType = MEDIA_TRACK_TYPE_TIMEDTEXT;
    } else {
        trackType = MEDIA_TRACK_TYPE_UNKNOWN;
    }
    format->setInt32("type", trackType);

    const char *lang;
    if (!meta->findCString(kKeyMediaLanguage, &lang)) {
        lang = "und";
    }
    format->setString("language", lang);

    if (trackType == MEDIA_TRACK_TYPE_SUBTITLE) {
        format->setString("mime", mime);

        int32_t isAutoselect = 1, isDefault = 0, isForced = 0;
        meta->findInt32(kKeyTrackIsAutoselect, &isAutoselect);
        meta->findInt32(kKeyTrackIsDefault, &isDefault);
        meta->findInt32(kKeyTrackIsForced, &isForced);

        format->setInt32("auto", !!isAutoselect);
        format->setInt32("default", !!isDefault);
        format->setInt32("forced", !!isForced);
    }

    return format;
}

status_t NuPlayer::GenericSource::selectTrack(size_t trackIndex, bool select) {
    ALOGV("selectTrack: %zu", trackIndex);
    if (trackIndex >= mSources.size()) {
        return BAD_INDEX;
    }

    if (!select) {
        if (mSubtitleTrack.mSource == NULL || trackIndex != mSubtitleTrack.mIndex) {
            return INVALID_OPERATION;
        }
        mSubtitleTrack.mSource = NULL;
        mSubtitleTrack.mPackets->clear();
        mFetchSubtitleDataGeneration++;
        return OK;
    }

    const sp<MediaSource> source = mSources.itemAt(trackIndex);
    sp<MetaData> meta = source->getFormat();
    const char *mime;
    CHECK(meta->findCString(kKeyMIMEType, &mime));
    if (!strncasecmp(mime, "text/", 5)) {
        if (mSubtitleTrack.mSource != NULL && mSubtitleTrack.mIndex == trackIndex) {
            return OK;
        }
        mSubtitleTrack.mIndex = trackIndex;
        mSubtitleTrack.mSource = mSources.itemAt(trackIndex);
        if (mSubtitleTrack.mPackets == NULL) {
            mSubtitleTrack.mPackets = new AnotherPacketSource(mSubtitleTrack.mSource->getFormat());
        } else {
            mSubtitleTrack.mPackets->clear();

        }
        mFetchSubtitleDataGeneration++;
        return OK;
    } else if (!strncasecmp(mime, "audio/", 6) || !strncasecmp(mime, "video/", 6)) {
        bool audio = !strncasecmp(mime, "audio/", 6);
        Track *track = audio ? &mAudioTrack : &mVideoTrack;
        if (track->mSource != NULL && track->mIndex == trackIndex) {
            return OK;
        }

        sp<AMessage> msg = new AMessage(kWhatChangeAVSource, id());
        msg->setInt32("trackIndex", trackIndex);
        msg->post();
        return OK;
    }

    return INVALID_OPERATION;
}

status_t NuPlayer::GenericSource::seekTo(int64_t seekTimeUs) {
    if (mVideoTrack.mSource != NULL) {
        int64_t actualTimeUs;
        readBuffer(MEDIA_TRACK_TYPE_VIDEO, seekTimeUs, &actualTimeUs);

        seekTimeUs = actualTimeUs;
    }

    if (mAudioTrack.mSource != NULL) {
        readBuffer(MEDIA_TRACK_TYPE_AUDIO, seekTimeUs);
    }

    return OK;
}

sp<ABuffer> NuPlayer::GenericSource::mediaBufferToABuffer(
        MediaBuffer* mb,
        media_track_type trackType,
        int64_t *actualTimeUs) {
    bool audio = trackType == MEDIA_TRACK_TYPE_AUDIO;
    size_t outLength = mb->range_length();

    if (audio && mAudioIsVorbis) {
        outLength += sizeof(int32_t);
    }

    sp<ABuffer> ab;
    if (mIsWidevine && !audio) {
        // data is already provided in the buffer
        ab = new ABuffer(NULL, mb->range_length());
        ab->meta()->setPointer("mediaBuffer", mb);
        mb->add_ref();
    } else {
        ab = new ABuffer(outLength);
        memcpy(ab->data(),
               (const uint8_t *)mb->data() + mb->range_offset(),
               mb->range_length());
    }

    if (audio && mAudioIsVorbis) {
        int32_t numPageSamples;
        if (!mb->meta_data()->findInt32(kKeyValidSamples, &numPageSamples)) {
            numPageSamples = -1;
        }

        uint8_t* abEnd = ab->data() + mb->range_length();
        memcpy(abEnd, &numPageSamples, sizeof(numPageSamples));
    }

    int64_t timeUs;
    CHECK(mb->meta_data()->findInt64(kKeyTime, &timeUs));

    sp<AMessage> meta = ab->meta();
    meta->setInt64("timeUs", timeUs);

    int64_t durationUs;
    if (mb->meta_data()->findInt64(kKeyDuration, &durationUs)) {
        meta->setInt64("durationUs", durationUs);
    }

    if (trackType == MEDIA_TRACK_TYPE_SUBTITLE) {
        meta->setInt32("trackIndex", mSubtitleTrack.mIndex);
    }

    if (actualTimeUs) {
        *actualTimeUs = timeUs;
    }

    mb->release();
    mb = NULL;

    return ab;
}

void NuPlayer::GenericSource::readBuffer(
        media_track_type trackType, int64_t seekTimeUs, int64_t *actualTimeUs, bool formatChange) {
    Track *track;
    switch (trackType) {
        case MEDIA_TRACK_TYPE_VIDEO:
            track = &mVideoTrack;
            break;
        case MEDIA_TRACK_TYPE_AUDIO:
            track = &mAudioTrack;
            break;
        case MEDIA_TRACK_TYPE_SUBTITLE:
            track = &mSubtitleTrack;
            break;
        default:
            TRESPASS();
    }

    if (track->mSource == NULL) {
        return;
    }

    if (actualTimeUs) {
        *actualTimeUs = seekTimeUs;
    }

    MediaSource::ReadOptions options;

    bool seeking = false;

    if (seekTimeUs >= 0) {
        options.setSeekTo(seekTimeUs, MediaSource::ReadOptions::SEEK_PREVIOUS_SYNC);
        seeking = true;
    }

    if (mIsWidevine && trackType != MEDIA_TRACK_TYPE_AUDIO) {
        options.setNonBlocking();
    }

    for (;;) {
        MediaBuffer *mbuf;
        status_t err = track->mSource->read(&mbuf, &options);

        options.clearSeekTo();

        if (err == OK) {
            // formatChange && seeking: track whose source is changed during selection
            // formatChange && !seeking: track whose source is not changed during selection
            // !formatChange: normal seek
            if ((seeking || formatChange) && trackType != MEDIA_TRACK_TYPE_SUBTITLE) {
                ATSParser::DiscontinuityType type = formatChange
                        ? (seeking
                                ? ATSParser::DISCONTINUITY_FORMATCHANGE
                                : ATSParser::DISCONTINUITY_NONE)
                        : ATSParser::DISCONTINUITY_SEEK;
                track->mPackets->queueDiscontinuity( type, NULL, true /* discard */);
            }

            sp<ABuffer> buffer = mediaBufferToABuffer(mbuf, trackType, actualTimeUs);
            track->mPackets->queueAccessUnit(buffer);
            break;
        } else if (err == WOULD_BLOCK) {
            break;
        } else if (err == INFO_FORMAT_CHANGED) {
#if 0
            track->mPackets->queueDiscontinuity(
                    ATSParser::DISCONTINUITY_FORMATCHANGE,
                    NULL,
                    false /* discard */);
#endif
        } else {
            track->mPackets->signalEOS(err);
            break;
        }
    }
}

}  // namespace android
