#include "audio_engine.h"
#include "audio_mixer.h"
#include <thread>
#include <chrono>

using namespace Halley;

AudioEngine::AudioEngine()
	: mixer(AudioMixer::makeMixer())
	, running(true)
	, needsBuffer(true)
{
}

AudioEngine::~AudioEngine()
{
}

void AudioEngine::playUI(size_t id, std::shared_ptr<AudioClip> clip, float volume, float pan, bool loop)
{
	addSource(id, std::make_unique<AudioSource>(clip, AudioSourcePosition::makeUI(pan), volume, loop));
}

void AudioEngine::playWorld(size_t id, std::shared_ptr<AudioClip> clip, Vector2f position, float volume, bool loop)
{
	addSource(id, std::make_unique<AudioSource>(clip, AudioSourcePosition::makePositional(Vector3f(position)), volume, loop));
}

void AudioEngine::setListener(Vector2f position)
{
	// TODO
}

void AudioEngine::run()
{
	const size_t bufSize = spec.numChannels * sizeof(AudioConfig::SampleFormat) * spec.bufferSize;
	const size_t minQueue = bufSize * 2;

	using namespace std::chrono_literals;
	while (out->getQueuedSize() >= minQueue && running) {
		std::this_thread::sleep_for(100us);
	}

	if (!running) {
		return;
	}

	//while (out->getQueuedSize() < minQueue && running) {
	generateBuffer();
	//}
}

void AudioEngine::addSource(size_t id, std::unique_ptr<AudioSource>&& src)
{
	sources.emplace_back(std::move(src));
	sources.back()->setId(id);
	idToSource[id] = sources.back().get();
}

AudioSource* AudioEngine::getSource(size_t id)
{
	auto src = idToSource.find(id);
	if (src != idToSource.end()) {
		return src->second;
	} else {
		return nullptr;
	}
}

std::vector<size_t> AudioEngine::getPlayingSounds()
{
	std::vector<size_t> result(idToSource.size());
	size_t i = 0;
	for (auto& kv: idToSource) {
		result[i++] = kv.first;
	}
	return result;
}

void AudioEngine::generateBuffer()
{
	updateSources();

	for (size_t i = 0; i < spec.numChannels; ++i) {
		mixChannel(i, channelBuffers[i].packs);
	}

	postUpdateSources();

	mixer->interleaveChannels(backBuffer, channelBuffers);
	out->queueAudio(backBuffer.packs);
}

void AudioEngine::start(AudioSpec s, AudioOutputAPI& o)
{
	spec = s;
	out = &o;

	const size_t bufferSize = spec.bufferSize / 16;
	backBuffer.packs.resize(bufferSize * spec.numChannels);

	tmpBuffer.packs.resize(bufferSize);
	channelBuffers.resize(spec.numChannels);
	for (auto& b: channelBuffers) {
		b.packs.resize(bufferSize);
	}

	channels.resize(spec.numChannels);
	channels[0].pan = 0.0f;
	channels[1].pan = 1.0f;
}

void AudioEngine::stop()
{
	running = false;
	needsBuffer = false;
	backBufferCondition.notify_one();
}

void AudioEngine::updateSources()
{
	for (auto& source: sources) {
		if (source->isPlaying()) {
			source->update(channels);
		} else if (!source->isDone() && source->isReady()) {
			source->start();
			source->update(channels);
		}
	}
}

void AudioEngine::postUpdateSources()
{
	size_t n = sources.size();
	for (size_t i = 0; i < n; ++i) {
		auto& source = sources[i];

		if (source->isPlaying()) {
			source->advancePlayback(backBuffer.packs.size() * 16 / spec.numChannels);
		}
		if (source->isDone()) {
			// Remove source
			idToSource.erase(source->getId());
			if (sources.size() > 1) {
				std::swap(source, sources.back());
			}
			sources.pop_back();
			--i;
			--n;
		}
	}
}

void AudioEngine::mixChannel(size_t channelNum, gsl::span<AudioSamplePack> dst)
{
	for (auto& pack : dst) {
		for (size_t i = 0; i < 16; ++i) {
			pack.samples[i] = 0.0f;
		}
	}
	for (auto& source : sources) {
		if (source->isPlaying()) {
			size_t nChannels = source->getNumberOfChannels();
			for (size_t i = 0; i < nChannels; ++i) {
				source->mixToBuffer(i, channelNum, tmpBuffer.packs, dst, *mixer);
			}
		}
	}
}