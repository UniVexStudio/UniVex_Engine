// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Transaction and Undo/Redo Service Implementation.
#include "prism/Transaction.h"
#include <algorithm>

namespace prism {

TransactionService::TransactionService(size_t maxHistory) : m_maxHistory(maxHistory) {}

void TransactionService::push(std::unique_ptr<IUndoCommand> cmd) {
  if (!cmd) return;

  if (m_activeTransaction) {
    m_activeTransaction->add(std::move(cmd));
    return;
  }

  m_undoStack.push_back(std::move(cmd));
  m_redoStack.clear();
  trim();

  if (onHistoryChanged) onHistoryChanged();
}

void TransactionService::push(std::string desc, std::function<void()> redoFn, std::function<void()> undoFn) {
  push(std::make_unique<LambdaUndoCommand>(std::move(desc), std::move(redoFn), std::move(undoFn)));
}

bool TransactionService::undo() {
  if (m_undoStack.empty()) return false;

  auto cmd = std::move(m_undoStack.back());
  m_undoStack.pop_back();

  cmd->undo();
  m_redoStack.push_back(std::move(cmd));

  if (onHistoryChanged) onHistoryChanged();
  return true;
}

bool TransactionService::redo() {
  if (m_redoStack.empty()) return false;

  auto cmd = std::move(m_redoStack.back());
  m_redoStack.pop_back();

  cmd->redo();
  m_undoStack.push_back(std::move(cmd));

  if (onHistoryChanged) onHistoryChanged();
  return true;
}

void TransactionService::beginTransaction(std::string name) {
  if (m_activeTransaction) {
    // If nested, commit the existing or keep grouping
    return;
  }
  m_activeTransaction = std::make_unique<CompoundUndoCommand>(std::move(name));
}

void TransactionService::endTransaction() {
  if (!m_activeTransaction) return;

  auto tx = std::move(m_activeTransaction);
  if (!tx->empty()) {
    m_undoStack.push_back(std::move(tx));
    m_redoStack.clear();
    trim();
    if (onHistoryChanged) onHistoryChanged();
  }
}

void TransactionService::cancelTransaction() {
  if (!m_activeTransaction) return;

  // Rollback anything executed within the active transaction
  m_activeTransaction->undo();
  m_activeTransaction.reset();
}

void TransactionService::clear() {
  m_undoStack.clear();
  m_redoStack.clear();
  m_activeTransaction.reset();
  m_cleanIndex = 0;
  if (onHistoryChanged) onHistoryChanged();
}

void TransactionService::trim() {
  if (m_undoStack.size() > m_maxHistory) {
    size_t excess = m_undoStack.size() - m_maxHistory;
    m_undoStack.erase(m_undoStack.begin(), m_undoStack.begin() + long(excess));
    if (m_cleanIndex >= 0) {
      m_cleanIndex = std::max(-1, m_cleanIndex - int(excess));
    }
  }
}

} // namespace prism
