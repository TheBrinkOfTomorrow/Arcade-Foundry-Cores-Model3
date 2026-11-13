#ifndef _MPEG_AUDIO_H_
#define _MPEG_AUDIO_H_

#include "Game.h"
#include <cstdint>

class CBlockFile;


namespace MpegDec
{
  void  LoadCustomTracks(const std::string &music_filepath, const Game &game);
  void	SetMemory(const uint8_t *data, int offset, int length, bool loop);
	void	UpdateMemory(const uint8_t *data, int offset, int length, bool loop);
	int		GetPosition();
	void	SetPosition(int pos);
	void	DecodeAudio(int16_t* left, int16_t* right, int numStereoSamples);
	void	Stop();
	bool	IsLoaded();

	// Arcade Foundry save states: the whole decoder (minimp3's state, the
	// frame being played out, the position in the MPEG ROM), so music
	// carries on to the sample after a load. rom: the MPEG ROM the buffer
	// points into. Load returns false when the block holds a custom track
	// (left to the position Supermodel restores).
	void	SaveDecoder(CBlockFile *file, const uint8_t *rom);
	bool	LoadDecoder(CBlockFile *file, const uint8_t *rom);
}

#endif

