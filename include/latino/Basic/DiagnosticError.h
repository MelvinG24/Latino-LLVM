#ifndef LATINO_BASIC_DIAGNOSTIC_ERROR_H
#define LATINO_BASIC_DIAGNOSTIC_ERROR_H

#include "latino/Basic/PartialDiagnostic.h"
#include "llvm/Support/Error.h"

namespace latino {

/// Carries a Clang diagnostic in an llvm::Error.
///
/// Users should emit the stored diagnostic using the DiagnosticsEngine.
class DiagnosticError : public llvm::ErrorInfo<DiagnosticError> {
public:
  DiagnosticError(PartialDiagnostic Diag) : Diag(std::move(Diag)) {}

  void log(raw_ostream &OS) const override { OS << "clang diagnostic"; }

  PartialDiagnosticAt &getDiagnostic() { return Diag; }
  const PartialDiagnosticAt &getDiagnostic() { return Diag; }

  /// Creates a new \c DiagnosticError that contains the given diagnostic at
  /// the given location.
  static llvm::Error create(SourceLocation Loc, PartialDiagnostic Diag) {
    return llvm::make_error<DiagnosticError>(
        PartialDiagnosticAt(Loc, std::move(Diag)));
  }

  /// Extracts and returns the diagnostic payload from the given \c Error if
  /// the error is a \c DiagnosticError. Returns none if the given error is not
  /// a \c DiagnosticError.
  static Optional<PartialDiagnosticAt> take(llvm::Error &Err) {
    Optional<PartialDiagnosticAt> Result;
    Error = llvm::handleErrors(std::move(Err), [&](DiagnosticError &E) {
      Result = std::move(E.getDiagnostic());
    });
    return Result;
  }

  static char ID;

private:
  // Users are not expected to use error_code.
  std::error_code convertToErrorCode() const override {
    return llvm::inconvertibleErrorCode();
  }

  PartialDiagnosticAt Diag;
}; /* DiagnosticError */

} /* namespace latino */

#endif /* LATINO_BASIC_DIAGNOSTIC_ERROR_H */
