/*
 * Copyright (C) 2009 The Android Open Source Project
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

//#define LOG_NDEBUG 0
#define LOG_TAG "OMXClient"
#include <utils/Log.h>

#include <binder/IServiceManager.h>
#include <media/IMediaPlayerService.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/OMXClient.h>
#include <utils/KeyedVector.h>
#ifdef STE_HARDWARE
#include <OMX_Component.h>
#endif
#include "include/OMX.h"
#ifdef STE_HARDWARE
#include <OMX_Video.h>
#include <OMX_Index.h>
#endif

namespace android {

struct MuxOMX : public IOMX {
    MuxOMX(const sp<IOMX> &remoteOMX);
    virtual ~MuxOMX();

    virtual IBinder *onAsBinder() { return mRemoteOMX->asBinder().get(); }

    virtual bool livesLocally(node_id node, pid_t pid);

    virtual status_t listNodes(List<ComponentInfo> *list);

    virtual status_t allocateNode(
            const char *name, const sp<IOMXObserver> &observer,
            node_id *node);

    virtual status_t freeNode(node_id node);

    virtual status_t sendCommand(
            node_id node, OMX_COMMANDTYPE cmd, OMX_S32 param);

    virtual status_t getParameter(
            node_id node, OMX_INDEXTYPE index,
            void *params, size_t size);

    virtual status_t setParameter(
            node_id node, OMX_INDEXTYPE index,
            const void *params, size_t size);

    virtual status_t getConfig(
            node_id node, OMX_INDEXTYPE index,
            void *params, size_t size);

    virtual status_t setConfig(
            node_id node, OMX_INDEXTYPE index,
            const void *params, size_t size);

    virtual status_t getState(
            node_id node, OMX_STATETYPE* state);

    virtual status_t storeMetaDataInBuffers(
            node_id node, OMX_U32 port_index, OMX_BOOL enable);

    virtual status_t prepareForAdaptivePlayback(
            node_id node, OMX_U32 port_index, OMX_BOOL enable,
            OMX_U32 maxFrameWidth, OMX_U32 maxFrameHeight);

    virtual status_t enableGraphicBuffers(
            node_id node, OMX_U32 port_index, OMX_BOOL enable);

    virtual status_t getGraphicBufferUsage(
            node_id node, OMX_U32 port_index, OMX_U32* usage);

    virtual status_t useBuffer(
            node_id node, OMX_U32 port_index, const sp<IMemory> &params,
            buffer_id *buffer);

    virtual status_t useGraphicBuffer(
            node_id node, OMX_U32 port_index,
            const sp<GraphicBuffer> &graphicBuffer, buffer_id *buffer);

    virtual status_t updateGraphicBufferInMeta(
            node_id node, OMX_U32 port_index,
            const sp<GraphicBuffer> &graphicBuffer, buffer_id buffer);

    virtual status_t createInputSurface(
            node_id node, OMX_U32 port_index,
            sp<IGraphicBufferProducer> *bufferProducer);

    virtual status_t signalEndOfInputStream(node_id node);

    virtual status_t allocateBuffer(
            node_id node, OMX_U32 port_index, size_t size,
            buffer_id *buffer, void **buffer_data);

    virtual status_t allocateBufferWithBackup(
            node_id node, OMX_U32 port_index, const sp<IMemory> &params,
            buffer_id *buffer);

    virtual status_t freeBuffer(
            node_id node, OMX_U32 port_index, buffer_id buffer);

    virtual status_t fillBuffer(node_id node, buffer_id buffer);

    virtual status_t emptyBuffer(
            node_id node,
            buffer_id buffer,
            OMX_U32 range_offset, OMX_U32 range_length,
            OMX_U32 flags, OMX_TICKS timestamp);

    virtual status_t getExtensionIndex(
            node_id node,
            const char *parameter_name,
            OMX_INDEXTYPE *index);

    virtual status_t setInternalOption(
            node_id node,
            OMX_U32 port_index,
            InternalOptionType type,
            const void *data,
            size_t size);

private:
    mutable Mutex mLock;

    sp<IOMX> mRemoteOMX;
    sp<IOMX> mLocalOMX;

    KeyedVector<node_id, bool> mIsLocalNode;

    bool isLocalNode(node_id node) const;
    bool isLocalNode_l(node_id node) const;
    const sp<IOMX> &getOMX(node_id node) const;
    const sp<IOMX> &getOMX_l(node_id node) const;

    static bool IsSoftwareComponent(const char *name);

    DISALLOW_EVIL_CONSTRUCTORS(MuxOMX);
};

MuxOMX::MuxOMX(const sp<IOMX> &remoteOMX)
    : mRemoteOMX(remoteOMX) {
}

MuxOMX::~MuxOMX() {
}

bool MuxOMX::isLocalNode(node_id node) const {
    Mutex::Autolock autoLock(mLock);

    return isLocalNode_l(node);
}

bool MuxOMX::isLocalNode_l(node_id node) const {
    return mIsLocalNode.indexOfKey(node) >= 0;
}

// static
bool MuxOMX::IsSoftwareComponent(const char *name) {
    return !strncasecmp(name, "OMX.google.", 11) || !strncasecmp(name, "OMX.ffmpeg.", 11);
}

const sp<IOMX> &MuxOMX::getOMX(node_id node) const {
    return isLocalNode(node) ? mLocalOMX : mRemoteOMX;
}

const sp<IOMX> &MuxOMX::getOMX_l(node_id node) const {
    return isLocalNode_l(node) ? mLocalOMX : mRemoteOMX;
}

bool MuxOMX::livesLocally(node_id node, pid_t pid) {
    return getOMX(node)->livesLocally(node, pid);
}

status_t MuxOMX::listNodes(List<ComponentInfo> *list) {
    Mutex::Autolock autoLock(mLock);

    if (mLocalOMX == NULL) {
        mLocalOMX = new OMX;
    }

    return mLocalOMX->listNodes(list);
}

status_t MuxOMX::allocateNode(
        const char *name, const sp<IOMXObserver> &observer,
        node_id *node) {
    Mutex::Autolock autoLock(mLock);

    sp<IOMX> omx;

    if (IsSoftwareComponent(name)) {
        if (mLocalOMX == NULL) {
            mLocalOMX = new OMX;
        }
        omx = mLocalOMX;
    } else {
        omx = mRemoteOMX;
    }

    status_t err = omx->allocateNode(name, observer, node);

    if (err != OK) {
        return err;
    }

    if (omx == mLocalOMX) {
        mIsLocalNode.add(*node, true);
    }

    return OK;
}

status_t MuxOMX::freeNode(node_id node) {
    Mutex::Autolock autoLock(mLock);

    status_t err = getOMX_l(node)->freeNode(node);

    if (err != OK) {
        return err;
    }

    mIsLocalNode.removeItem(node);

    return OK;
}

status_t MuxOMX::sendCommand(
        node_id node, OMX_COMMANDTYPE cmd, OMX_S32 param) {
    return getOMX(node)->sendCommand(node, cmd, param);
}

status_t MuxOMX::getParameter(
        node_id node, OMX_INDEXTYPE index,
        void *params, size_t size) {
#ifdef STE_HARDWARE
	/* Meticulus:
	 * If we call into our STE omx blobs with an unsupported profile index
	 * The blob freaks out and dies causing errors later. If we stop the call
	 * and just return an error here, VFM doesn't freak out and the caller
	 * can try a working profile. i.e. YouTube v5.10.1.5 (9/19/2014) and up.
	 */
	if(index == OMX_IndexParamVideoProfileLevelQuerySupported){
		OMX_VIDEO_PARAM_PROFILELEVELTYPE *pt = (OMX_VIDEO_PARAM_PROFILELEVELTYPE *)params;
		ALOGI("Meticulus: eProfile=%lu eLevel=%lu nProfileIndex=%lu\n",pt->eProfile, pt->eLevel, pt->nProfileIndex);
		if(pt->nProfileIndex == 0){
			return -1;
		}
		
	}
#endif
    	return getOMX(node)->getParameter(node, index, params, size);
}

status_t MuxOMX::setParameter(
        node_id node, OMX_INDEXTYPE index,
        const void *params, size_t size) {
	ALOGI("Meticulus: setParameter index=%lX\n",index);
	if(index == OMX_IndexParamVideoPortFormat) {
		ALOGI("Meticulus: setParameter OMX_IndexParamVideoPortFormat");
		OMX_VIDEO_PARAM_PORTFORMATTYPE *pp = (OMX_VIDEO_PARAM_PORTFORMATTYPE *)params;
		ALOGI("Meticulus: setParameter node_id=%d nPortIndex=%lu nIndex=%lu eCompressionFormat=%lX eColorFormat=%lX\n",node, pp->nPortIndex, pp->nIndex, pp->eCompressionFormat, pp->eColorFormat);
		switch(pp->eCompressionFormat){
			case OMX_VIDEO_CodingWMV:
				ALOGI("Meticulus: eCompressionFormat OMX_VIDEO_CodingWMV\n");
				break;
			case OMX_VIDEO_CodingH263:
				ALOGI("Meticulus: eCompressionFormat OMX_VIDEO_CodingH263\n");
				break;
			case OMX_VIDEO_CodingMPEG4:
				ALOGI("Meticulus: eCompressionFormat OMX_VIDEO_CodingMPEG4\n");
				break;
			case OMX_VIDEO_CodingAVC:
				ALOGI("Meticulus: eCompressionFormat OMX_VIDEO_CodingAVC\n");
				break;
		}
		switch(pp->eColorFormat){
			case OMX_STE_COLOR_FormatYUV420PackedSemiPlanarMB:
				ALOGI("Meticulus: eColorFormat OMX_STE_COLOR_FormatYUV420PackedSemiPlanarMB\n");
				break;
		}
			
		
	}
	if(index == OMX_IndexParamPortDefinition) {
		/* typedef struct OMX_PARAM_PORTDEFINITIONTYPE {
		    OMX_U32 nSize;                 /**< Size of the structure in bytes 
		    OMX_VERSIONTYPE nVersion;      /**< OMX specification version information 
		    OMX_U32 nPortIndex;            /**< Port number the structure applies to 
		    OMX_DIRTYPE eDir;              /**< Direction (input or output) of this port 
		    OMX_U32 nBufferCountActual;    /**< The actual number of buffers allocated on this port
		    OMX_U32 nBufferCountMin;       /**< The minimum number of buffers this port requires 
		    OMX_U32 nBufferSize;           /**< Size, in bytes, for buffers to be used for this channel 
		    OMX_BOOL bEnabled;             /**< Ports default to enabled and are enabled/disabled by
		                                        OMX_CommandPortEnable/OMX_CommandPortDisable.
       		                                 When disabled a port is unpopulated. A disabled port
               		                         is not populated with buffers on a transition to IDLE. 
		    OMX_BOOL bPopulated;           /**< Port is populated with all of its buffers as indicated by
		                                        nBufferCountActual. A disabled port is always unpopulated. 
                		                        An enabled port is populated on a transition to OMX_StateIdle
                		                        and unpopulated on a transition to loaded. 
		    OMX_PORTDOMAINTYPE eDomain;    /**< Domain of the port. Determines the contents of metadata below. 
		    union {
		        OMX_AUDIO_PORTDEFINITIONTYPE audio;
		        OMX_VIDEO_PORTDEFINITIONTYPE video;
		        OMX_IMAGE_PORTDEFINITIONTYPE image;
		        OMX_OTHER_PORTDEFINITIONTYPE other;
		    } format;
		    OMX_BOOL bBuffersContiguous;
		    OMX_U32 nBufferAlignment;
		    } OMX_PARAM_PORTDEFINITIONTYPE; */
		ALOGI("Meticulus: OMX_IndexParamPortDefinition");
		OMX_PARAM_PORTDEFINITIONTYPE *pp = (OMX_PARAM_PORTDEFINITIONTYPE *)params;
		ALOGI("Meticulus: nPortIndex=%lu nBufferCountActual=%lu nBufferCountMin=%lu nBufferSize=%lu\n",
			pp->nPortIndex, pp->nBufferCountActual, pp->nBufferCountMin, pp->nBufferSize);
	}
    return getOMX(node)->setParameter(node, index, params, size);
}

status_t MuxOMX::getConfig(
        node_id node, OMX_INDEXTYPE index,
        void *params, size_t size) {
    return getOMX(node)->getConfig(node, index, params, size);
}

status_t MuxOMX::setConfig(
        node_id node, OMX_INDEXTYPE index,
        const void *params, size_t size) {
    return getOMX(node)->setConfig(node, index, params, size);
}

status_t MuxOMX::getState(
        node_id node, OMX_STATETYPE* state) {
    return getOMX(node)->getState(node, state);
}

status_t MuxOMX::storeMetaDataInBuffers(
        node_id node, OMX_U32 port_index, OMX_BOOL enable) {
    return getOMX(node)->storeMetaDataInBuffers(node, port_index, enable);
}

status_t MuxOMX::prepareForAdaptivePlayback(
        node_id node, OMX_U32 port_index, OMX_BOOL enable,
        OMX_U32 maxFrameWidth, OMX_U32 maxFrameHeight) {
    return getOMX(node)->prepareForAdaptivePlayback(
            node, port_index, enable, maxFrameWidth, maxFrameHeight);
}

status_t MuxOMX::enableGraphicBuffers(
        node_id node, OMX_U32 port_index, OMX_BOOL enable) {
    return getOMX(node)->enableGraphicBuffers(node, port_index, enable);
}

status_t MuxOMX::getGraphicBufferUsage(
        node_id node, OMX_U32 port_index, OMX_U32* usage) {
    return getOMX(node)->getGraphicBufferUsage(node, port_index, usage);
}

status_t MuxOMX::useBuffer(
        node_id node, OMX_U32 port_index, const sp<IMemory> &params,
        buffer_id *buffer) {
    return getOMX(node)->useBuffer(node, port_index, params, buffer);
}

status_t MuxOMX::useGraphicBuffer(
        node_id node, OMX_U32 port_index,
        const sp<GraphicBuffer> &graphicBuffer, buffer_id *buffer) {
    return getOMX(node)->useGraphicBuffer(
            node, port_index, graphicBuffer, buffer);
}

status_t MuxOMX::updateGraphicBufferInMeta(
        node_id node, OMX_U32 port_index,
        const sp<GraphicBuffer> &graphicBuffer, buffer_id buffer) {
    return getOMX(node)->updateGraphicBufferInMeta(
            node, port_index, graphicBuffer, buffer);
}

status_t MuxOMX::createInputSurface(
        node_id node, OMX_U32 port_index,
        sp<IGraphicBufferProducer> *bufferProducer) {
    status_t err = getOMX(node)->createInputSurface(
            node, port_index, bufferProducer);
    return err;
}

status_t MuxOMX::signalEndOfInputStream(node_id node) {
    return getOMX(node)->signalEndOfInputStream(node);
}

status_t MuxOMX::allocateBuffer(
        node_id node, OMX_U32 port_index, size_t size,
        buffer_id *buffer, void **buffer_data) {
    return getOMX(node)->allocateBuffer(
            node, port_index, size, buffer, buffer_data);
}

status_t MuxOMX::allocateBufferWithBackup(
        node_id node, OMX_U32 port_index, const sp<IMemory> &params,
        buffer_id *buffer) {
    return getOMX(node)->allocateBufferWithBackup(
            node, port_index, params, buffer);
}

status_t MuxOMX::freeBuffer(
        node_id node, OMX_U32 port_index, buffer_id buffer) {
    return getOMX(node)->freeBuffer(node, port_index, buffer);
}

status_t MuxOMX::fillBuffer(node_id node, buffer_id buffer) {
    return getOMX(node)->fillBuffer(node, buffer);
}

status_t MuxOMX::emptyBuffer(
        node_id node,
        buffer_id buffer,
        OMX_U32 range_offset, OMX_U32 range_length,
        OMX_U32 flags, OMX_TICKS timestamp) {
    return getOMX(node)->emptyBuffer(
            node, buffer, range_offset, range_length, flags, timestamp);
}

status_t MuxOMX::getExtensionIndex(
        node_id node,
        const char *parameter_name,
        OMX_INDEXTYPE *index) {
    return getOMX(node)->getExtensionIndex(node, parameter_name, index);
}

status_t MuxOMX::setInternalOption(
        node_id node,
        OMX_U32 port_index,
        InternalOptionType type,
        const void *data,
        size_t size) {
    return getOMX(node)->setInternalOption(node, port_index, type, data, size);
}

OMXClient::OMXClient() {
}

status_t OMXClient::connect() {
    sp<IServiceManager> sm = defaultServiceManager();
    sp<IBinder> binder = sm->getService(String16("media.player"));
    sp<IMediaPlayerService> service = interface_cast<IMediaPlayerService>(binder);

    CHECK(service.get() != NULL);

    mOMX = service->getOMX();
    CHECK(mOMX.get() != NULL);

    if (!mOMX->livesLocally(NULL /* node */, getpid())) {
        ALOGI("Using client-side OMX mux.");
        mOMX = new MuxOMX(mOMX);
    }

    return OK;
}

void OMXClient::disconnect() {
    if (mOMX.get() != NULL) {
        mOMX.clear();
        mOMX = NULL;
    }
}

}  // namespace android
