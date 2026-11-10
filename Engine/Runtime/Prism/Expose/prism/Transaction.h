// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Transaction and Undo/Redo Service.
// Supports atomic compound transactions, command stacks, and change notifications.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace prism {

class IUndoCommand {
public:
  virtual ~IUndoCommand() = default;
  virtual void execute() = 0;
  virtual void undo() = 0;
  virtual void redo() { execute(); }
  virtual std::string description() const = 0;
  virtual size_t memoryUsage() const { return sizeof(*this); }
};

class LambdaUndoCommand : public IUndoCommand {
public:
  LambdaUndoCommand(std::string desc, std::function<void()> redoFn, std::function<void()> undoFn)
      : m_desc(std::move(desc)), m_redoFn(std::move(redoFn)), m_undoFn(std::move(undoFn)) {}

  void execute() override {
    if (m_redoFn) m_redoFn();
  }
  void undo() override {
    if (m_undoFn) m_undoFn();
  }
  std::string description() const override { return m_desc; }

private:
  std::string m_desc;
  std::function<void()> m_redoFn;
  std::function<void()> m_undoFn;
};

// Compound command that groups multiple micro-actions into one atomic undo step
class CompoundUndoCommand : public IUndoCommand {
public:
  explicit CompoundUndoCommand(std::string desc = "") : m_desc(std::move(desc)) {}

  void add(std::unique_ptr<IUndoCommand> cmd) {
    if (cmd) m_commands.push_back(std::move(cmd));
  }

  void execute() override {
    for (auto& cmd : m_commands) cmd->execute();
  }

  void undo() override {
    // Undo in reverse order
    for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it) {
      (*it)->undo();
    }
  }

  void redo() override {
    for (auto& cmd : m_commands) cmd->redo();
  }

  std::string description() const override { return m_desc; }
  size_t count() const { return m_commands.size(); }
  bool empty() const { return m_commands.empty(); }

private:
  std::string m_desc;
  std::vector<std::unique_ptr<IUndoCommand>> m_commands;
};

class TransactionService {
public:
  explicit TransactionService(size_t maxHistory = 100);

  // Push an executed command to the undo stack
  void push(std::unique_ptr<IUndoCommand> cmd);
  void push(std::string desc, std::function<void()> redoFn, std::function<void()> undoFn);

  // Undo / Redo operations
  bool undo();
  bool redo();
  bool canUndo() const { return !m_undoStack.empty(); }
  bool canRedo() const { return !m_redoStack.empty(); }

  std::string undoDescription() const {
    return canUndo() ? m_undoStack.back()->description() : "";
  }
  std::string redoDescription() const {
    return canRedo() ? m_redoStack.back()->description() : "";
  }

  // Transaction grouping
  void beginTransaction(std::string name);
  void endTransaction();
  void cancelTransaction();
  bool inTransaction() const { return m_activeTransaction != nullptr; }

  // Clean / dirty tracking
  void markClean() { m_cleanIndex = int(m_undoStack.size()); }
  bool isClean() const { return m_cleanIndex == int(m_undoStack.size()); }

  void clear();
  void setMaxHistory(size_t maxCount) { m_maxHistory = maxCount; trim(); }
  size_t undoCount() const { return m_undoStack.size(); }
  size_t redoCount() const { return m_redoStack.size(); }

  std::function<void()> onHistoryChanged;

private:
  void trim();

  size_t m_maxHistory;
  int m_cleanIndex = 0;
  std::vector<std::unique_ptr<IUndoCommand>> m_undoStack;
  std::vector<std::unique_ptr<IUndoCommand>> m_redoStack;
  std::unique_ptr<CompoundUndoCommand> m_activeTransaction;
};

// RAII Transaction Scope helper
class ScopedTransaction {
public:
  ScopedTransaction(TransactionService& service, std::string name)
      : m_service(service), m_committed(false) {
    m_service.beginTransaction(std::move(name));
  }

  ~ScopedTransaction() {
    if (!m_committed) m_service.cancelTransaction();
  }

  void commit() {
    if (!m_committed) {
      m_service.endTransaction();
      m_committed = true;
    }
  }

  void cancel() {
    if (!m_committed) {
      m_service.cancelTransaction();
      m_committed = true;
    }
  }

private:
  TransactionService& m_service;
  bool m_committed;
};

} // namespace prism
