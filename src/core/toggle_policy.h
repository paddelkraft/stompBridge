#pragma once

#include "contracts.h"

namespace stomp {

struct TogglePlan {
  Result result = Result::Unsupported;
  Command command;
  bool awaitingReadback = false;
};

// This chooses the next action only. The session scheduler owns the readback
// timeout and must report TimedOut without silently issuing a set command.
inline TogglePlan planToggle(BlockId id, BooleanValue observed,
                             bool readbackSupported, OperationId operation,
                             SessionGeneration generation) {
  TogglePlan plan;
  plan.result = Result::Accepted;
  plan.command.target = id;
  plan.command.operation = operation;
  plan.command.generation = generation;
  if (observed.certainty == Certainty::Unknown && readbackSupported) {
    plan.command.kind = CommandKind::RequestReadback;
    plan.awaitingReadback = true;
  } else {
    plan.command.kind = CommandKind::SetBlock;
    plan.command.enabled = observed.certainty == Certainty::Unknown ||
                           !observed.value;
  }
  return plan;
}

inline Result readbackTimeoutResult(const TogglePlan &plan) {
  return plan.awaitingReadback ? Result::TimedOut : Result::Invalid;
}

} // namespace stomp
