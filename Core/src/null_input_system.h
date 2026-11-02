/*
 * Arcade Foundry core: an input system with no devices. The core writes each
 * input's value itself from the front end's afcore_input, so nothing is
 * mapped or polled here.
 */
#ifndef ATTRACT_NULL_INPUT_SYSTEM_H
#define ATTRACT_NULL_INPUT_SYSTEM_H

#include "Inputs/InputSystem.h"

class CNullInputSystem : public CInputSystem
{
public:
  CNullInputSystem() : CInputSystem("Arcade Foundry") {}

protected:
  bool InitializeSystem() override { return true; }
  int GetKeyIndex(const char *) override { return -1; }
  const char *GetKeyName(int) override { return nullptr; }
  bool IsKeyPressed(int, int) const override { return false; }
  int GetMouseAxisValue(int, int) const override { return 0; }
  int GetMouseWheelDir(int) const override { return 0; }
  bool IsMouseButPressed(int, int) const override { return false; }
  int GetJoyAxisValue(int, int) const override { return 0; }
  bool IsJoyPOVInDir(int, int, int) const override { return false; }
  bool IsJoyButPressed(int, int) const override { return false; }
  bool ProcessForceFeedbackCmd(int, int, ForceFeedbackCmd) override { return false; }

public:
  int GetNumKeyboards() const override { return 0; }
  int GetNumMice() const override { return 0; }
  int GetNumJoysticks() const override { return 0; }
  const KeyDetails *GetKeyDetails(int) override { return nullptr; }
  const MouseDetails *GetMouseDetails(int) override { return nullptr; }
  const JoyDetails *GetJoyDetails(int) override { return nullptr; }
  bool Poll() override { return true; }
  void SetMouseVisibility(bool) override {}
};

#endif
