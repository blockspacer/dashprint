#ifndef _MP4STREAMER_H
#define _MP4STREAMER_H
#include <string_view>
#include <memory>
#include <functional>
#include <stdint.h>

extern "C" {
#include <libavformat/avformat.h>
}

// Annex-B H.264 source
class H264Source
{
public:
	virtual ~H264Source() {}
	virtual int read(uint8_t* buf, int bufSize) = 0;
};

class MP4Sink
{
public:
	virtual int write(const uint8_t* buf, int bufSize) = 0;
};

class MP4SinkLambda : public MP4Sink
{
public:
	typedef std::function<int(const uint8_t* buf, int bufSize)> lambda_t;
	MP4SinkLambda(lambda_t l) : m_lambda(l)
	{
	}
	int write(const uint8_t* buf, int bufSize) override
	{
		return m_lambda(buf, bufSize);
	}
private:
	lambda_t m_lambda;
};

class MP4Streamer
{
public:
	MP4Streamer(std::shared_ptr<H264Source> source, MP4Sink* target);
	~MP4Streamer();
	void run();
	void stop() { m_stop = true; }
private:
	static int readPacket(void* opaque, uint8_t* buf, int bufSize);
	static int writePacket(void* opaque, uint8_t* buf, int bufSize);
	static int throwAverror(const char* descr, int err);
private:
	std::shared_ptr<H264Source> m_source;
	MP4Sink* m_target;
	AVIOContext* m_avioContextIn = nullptr;
	AVIOContext* m_avioContextOut = nullptr;
	AVFormatContext* m_inputFormatContext = nullptr;
	AVFormatContext* m_outputFormatContext = nullptr;
	bool m_stop = false;
};

#endif
