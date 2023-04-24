//===- ComputeDependence.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "latino/AST/ComputeDependence.h"
#include "latino/AST/Attr.h"
#include "latino/AST/DeclCXX.h"
#include "latino/AST/DeclarationName.h"
#include "latino/AST/DependenceFlags.h"
#include "latino/AST/Expr.h"
#include "latino/AST/ExprCXX.h"
#include "latino/AST/ExprConcepts.h"
// #include "latino/AST/ExprObjC.h"
#include "latino/AST/ExprOpenMP.h"
#include "latino/Basic/ExceptionSpecificationType.h"
#include "llvm/ADT/ArrayRef.h"

using namespace latino;

ExprDependence latino::computeDependence(FullExpr *E) {
  return E->getSubExpr()->getDependence();
}

ExprDependence latino::computeDependence(OpaqueValueExpr *E) {
  auto D = toExprDependence(E->getType()->getDependence());
  if (auto *S = E->getSourceExpr())
    D |= S->getDependence();
  assert(!(D & ExprDependence::UnexpandedPack));
  return D;
}

ExprDependence latino::computeDependence(ParenExpr *E) {
  return E->getSubExpr()->getDependence();
}

ExprDependence latino::computeDependence(UnaryOperator *E) {
  return toExprDependence(E->getType()->getDependence()) |
         E->getSubExpr()->getDependence();
}

ExprDependence latino::computeDependence(UnaryExprOrTypeTraitExpr *E) {
  // Never type-dependent (C++ [temp.dep.expr]p3).
  // Value-dependent if the argument is type-dependent.
  if (E->isArgumentType())
    return turnTypeToValueDependence(
        toExprDependence(E->getArgumentType()->getDependence()));

  auto ArgDeps = E->getArgumentExpr()->getDependence();
  auto Deps = ArgDeps & ~ExprDependence::TypeValue;
  // Value-dependent if the argument is type-dependent.
  if (ArgDeps & ExprDependence::Type)
    Deps |= ExprDependence::Value;
  // Check to see if we are in the situation where alignof(decl) should be
  // dependent because decl's alignment is dependent.
  auto ExprKind = E->getKind();
  if (ExprKind != UETT_AlignOf && ExprKind != UETT_PreferredAlignOf)
    return Deps;
  if ((Deps & ExprDependence::Value) && (Deps & ExprDependence::Instantiation))
    return Deps;

  auto *NoParens = E->getArgumentExpr()->IgnoreParens();
  const ValueDecl *D = nullptr;
  if (const auto *DRE = dyn_cast<DeclRefExpr>(NoParens))
    D = DRE->getDecl();
  else if (const auto *ME = dyn_cast<MemberExpr>(NoParens))
    D = ME->getMemberDecl();
  if (!D)
    return Deps;
  for (const auto *I : D->specific_attrs<AlignedAttr>()) {
    if (I->isAlignmentErrorDependent())
      Deps |= ExprDependence::Error;
    if (I->isAlignmentDependent())
      Deps |= ExprDependence::ValueInstantiation;
  }
  return Deps;
}

ExprDependence latino::computeDependence(ArraySubscriptExpr *E) {
  return E->getLHS()->getDependence() | E->getRHS()->getDependence();
}

ExprDependence latino::computeDependence(MatrixSubscriptExpr *E) {
  return E->getBase()->getDependence() | E->getRowIdx()->getDependence() |
         (E->getColumnIdx() ? E->getColumnIdx()->getDependence()
                            : ExprDependence::None);
}

ExprDependence latino::computeDependence(CompoundLiteralExpr *E) {
  return toExprDependence(E->getTypeSourceInfo()->getType()->getDependence()) |
         turnTypeToValueDependence(E->getInitializer()->getDependence());
}

ExprDependence latino::computeDependence(CastExpr *E) {
  // Cast expressions are type-dependent if the type is
  // dependent (C++ [temp.dep.expr]p3).
  // Cast expressions are value-dependent if the type is
  // dependent or if the subexpression is value-dependent.
  auto D = toExprDependence(E->getType()->getDependence());
  if (E->getStmtClass() == Stmt::ImplicitCastExprClass) {
    // An implicit cast expression doesn't (lexically) contain an
    // unexpanded pack, even if its target type does.
    D &= ~ExprDependence::UnexpandedPack;
  }
  if (auto *S = E->getSubExpr())
    D |= S->getDependence() & ~ExprDependence::Type;
  return D;
}

ExprDependence latino::computeDependence(BinaryOperator *E) {
  return E->getLHS()->getDependence() | E->getRHS()->getDependence();
}

ExprDependence latino::computeDependence(ConditionalOperator *E) {
  // The type of the conditional operator depends on the type of the conditional
  // to support the GCC vector conditional extension. Additionally,
  // [temp.dep.expr] does specify state that this should be dependent on ALL sub
  // expressions.
  return E->getCond()->getDependence() | E->getLHS()->getDependence() |
         E->getRHS()->getDependence();
}

ExprDependence latino::computeDependence(BinaryConditionalOperator *E) {
  return E->getCommon()->getDependence() | E->getFalseExpr()->getDependence();
}

ExprDependence latino::computeDependence(StmtExpr *E, unsigned TemplateDepth) {
  auto D = toExprDependence(E->getType()->getDependence());
  // Propagate dependence of the result.
  if (const auto *CompoundExprResult =
          dyn_cast_or_null<ValueStmt>(E->getSubStmt()->getStmtExprResult()))
    if (const Expr *ResultExpr = CompoundExprResult->getExprStmt())
      D |= ResultExpr->getDependence();
  // Note: we treat a statement-expression in a dependent context as always
  // being value- and instantiation-dependent. This matches the behavior of
  // lambda-expressions and GCC.
  if (TemplateDepth)
    D |= ExprDependence::ValueInstantiation;
  // A param pack cannot be expanded over stmtexpr boundaries.
  return D & ~ExprDependence::UnexpandedPack;
}

ExprDependence latino::computeDependence(ConvertVectorExpr *E) {
  auto D = toExprDependence(E->getType()->getDependence()) |
           E->getSrcExpr()->getDependence();
  if (!E->getType()->isDependentType())
    D &= ~ExprDependence::Type;
  return D;
}

ExprDependence latino::computeDependence(ChooseExpr *E) {
  if (E->isConditionDependent())
    return ExprDependence::TypeValueInstantiation |
           E->getCond()->getDependence() | E->getLHS()->getDependence() |
           E->getRHS()->getDependence();

  auto Cond = E->getCond()->getDependence();
  auto Active = E->getLHS()->getDependence();
  auto Inactive = E->getRHS()->getDependence();
  if (!E->isConditionTrue())
    std::swap(Active, Inactive);
  // Take type- and value- dependency from the active branch. Propagate all
  // other flags from all branches.
  return (Active & ExprDependence::TypeValue) |
         ((Cond | Active | Inactive) & ~ExprDependence::TypeValue);
}

ExprDependence latino::computeDependence(ParenListExpr *P) {
  auto D = ExprDependence::None;
  for (auto *E : P->exprs())
    D |= E->getDependence();
  return D;
}

ExprDependence latino::computeDependence(VAArgExpr *E) {
  auto D =
      toExprDependence(E->getWrittenTypeInfo()->getType()->getDependence()) |
      (E->getSubExpr()->getDependence() & ~ExprDependence::Type);
  return D & ~ExprDependence::Value;
}

ExprDependence latino::computeDependence(NoInitExpr *E) {
  return toExprDependence(E->getType()->getDependence()) &
         (ExprDependence::Instantiation | ExprDependence::Error);
}

ExprDependence latino::computeDependence(ArrayInitLoopExpr *E) {
  auto D = E->getCommonExpr()->getDependence() |
           E->getSubExpr()->getDependence() | ExprDependence::Instantiation;
  if (!E->getType()->isInstantiationDependentType())
    D &= ~ExprDependence::Instantiation;
  return turnTypeToValueDependence(D);
}

ExprDependence latino::computeDependence(ImplicitValueInitExpr *E) {
  return toExprDependence(E->getType()->getDependence()) &
         ExprDependence::Instantiation;
}

ExprDependence latino::computeDependence(ExtVectorElementExpr *E) {
  return E->getBase()->getDependence();
}

ExprDependence latino::computeDependence(BlockExpr *E) {
  auto D = toExprDependence(E->getType()->getDependence());
  if (E->getBlockDecl()->isDependentContext())
    D |= ExprDependence::Instantiation;
  return D & ~ExprDependence::UnexpandedPack;
}

ExprDependence latino::computeDependence(AsTypeExpr *E) {
  auto D = toExprDependence(E->getType()->getDependence()) |
           E->getSrcExpr()->getDependence();
  if (!E->getType()->isDependentType())
    D &= ~ExprDependence::Type;
  return D;
}

ExprDependence latino::computeDependence(CXXRewrittenBinaryOperator *E) {
  return E->getSemanticForm()->getDependence();
}

ExprDependence latino::computeDependence(CXXStdInitializerListExpr *E) {
  auto D = turnTypeToValueDependence(E->getSubExpr()->getDependence());
  D |= toExprDependence(E->getType()->getDependence()) &
       (ExprDependence::Type | ExprDependence::Error);
  return D;
}

ExprDependence latino::computeDependence(CXXTypeidExpr *E) {
  auto D = ExprDependence::None;
  if (E->isTypeOperand())
    D = toExprDependence(
        E->getTypeOperandSourceInfo()->getType()->getDependence());
  else
    D = turnTypeToValueDependence(E->getExprOperand()->getDependence());
  // typeid is never type-dependent (C++ [temp.dep.expr]p4)
  return D & ~ExprDependence::Type;
}

ExprDependence latino::computeDependence(MSPropertyRefExpr *E) {
  return E->getBaseExpr()->getDependence() & ~ExprDependence::Type;
}

ExprDependence latino::computeDependence(MSPropertySubscriptExpr *E) {
  return E->getIdx()->getDependence();
}

ExprDependence latino::computeDependence(CXXUuidofExpr *E) {
  if (E->isTypeOperand())
    return turnTypeToValueDependence(toExprDependence(
        E->getTypeOperandSourceInfo()->getType()->getDependence()));

  return turnTypeToValueDependence(E->getExprOperand()->getDependence());
}

ExprDependence latino::computeDependence(CXXThisExpr *E) {
  // 'this' is type-dependent if the class type of the enclosing
  // member function is dependent (C++ [temp.dep.expr]p2)
  auto D = toExprDependence(E->getType()->getDependence());
  assert(!(D & ExprDependence::UnexpandedPack));
  return D;
}

ExprDependence latino::computeDependence(CXXThrowExpr *E) {
  auto *Op = E->getSubExpr();
  if (!Op)
    return ExprDependence::None;
  return Op->getDependence() & ~ExprDependence::TypeValue;
}

ExprDependence latino::computeDependence(CXXBindTemporaryExpr *E) {
  return E->getSubExpr()->getDependence();
}

ExprDependence latino::computeDependence(CXXScalarValueInitExpr *E) {
  return toExprDependence(E->getType()->getDependence()) &
         ~ExprDependence::TypeValue;
}

ExprDependence latino::computeDependence(CXXDeleteExpr *E) {
  return turnTypeToValueDependence(E->getArgument()->getDependence());
}

ExprDependence latino::computeDependence(ArrayTypeTraitExpr *E) {
  auto D = toExprDependence(E->getQueriedType()->getDependence());
  if (auto *Dim = E->getDimensionExpression())
    D |= Dim->getDependence();
  return turnTypeToValueDependence(D);
}

ExprDependence latino::computeDependence(ExpressionTraitExpr *E) {
  // Never type-dependent.
  auto D = E->getQueriedExpression()->getDependence() & ~ExprDependence::Type;
  // Value-dependent if the argument is type-dependent.
  if (E->getQueriedExpression()->isTypeDependent())
    D |= ExprDependence::Value;
  return D;
}

ExprDependence latino::computeDependence(CXXNoexceptExpr *E, CanThrowResult CT) {
  auto D = E->getOperand()->getDependence() & ~ExprDependence::TypeValue;
  if (CT == CT_Dependent)
    D |= ExprDependence::ValueInstantiation;
  return D;
}

ExprDependence latino::computeDependence(PackExpansionExpr *E) {
  return (E->getPattern()->getDependence() & ~ExprDependence::UnexpandedPack) |
         ExprDependence::TypeValueInstantiation;
}

ExprDependence latino::computeDependence(SubstNonTypeTemplateParmExpr *E) {
  return E->getReplacement()->getDependence();
}

ExprDependence latino::computeDependence(CoroutineSuspendExpr *E) {
  if (auto *Resume = E->getResumeExpr())
    return (Resume->getDependence() &
            (ExprDependence::TypeValue | ExprDependence::Error)) |
           (E->getCommonExpr()->getDependence() & ~ExprDependence::TypeValue);
  return E->getCommonExpr()->getDependence() |
         ExprDependence::TypeValueInstantiation;
}

ExprDependence latino::computeDependence(DependentCoawaitExpr *E) {
  return E->getOperand()->getDependence() |
         ExprDependence::TypeValueInstantiation;
}

// ExprDependence latino::computeDependence(ObjCBoxedExpr *E) {
//   return E->getSubExpr()->getDependence();
// }

// ExprDependence latino::computeDependence(ObjCEncodeExpr *E) {
//   return toExprDependence(E->getEncodedType()->getDependence());
// }

// ExprDependence latino::computeDependence(ObjCIvarRefExpr *E) {
//   return turnTypeToValueDependence(E->getBase()->getDependence());
// }

// ExprDependence latino::computeDependence(ObjCPropertyRefExpr *E) {
//   if (E->isObjectReceiver())
//     return E->getBase()->getDependence() & ~ExprDependence::Type;
//   if (E->isSuperReceiver())
//     return toExprDependence(E->getSuperReceiverType()->getDependence()) &
//            ~ExprDependence::TypeValue;
//   assert(E->isClassReceiver());
//   return ExprDependence::None;
// }

// ExprDependence latino::computeDependence(ObjCSubscriptRefExpr *E) {
//   return E->getBaseExpr()->getDependence() | E->getKeyExpr()->getDependence();
// }

// ExprDependence latino::computeDependence(ObjCIsaExpr *E) {
//   return E->getBase()->getDependence() & ~ExprDependence::Type &
//          ~ExprDependence::UnexpandedPack;
// }

// ExprDependence latino::computeDependence(ObjCIndirectCopyRestoreExpr *E) {
//   return E->getSubExpr()->getDependence();
// }

ExprDependence latino::computeDependence(OMPArraySectionExpr *E) {
  auto D = E->getBase()->getDependence();
  if (auto *LB = E->getLowerBound())
    D |= LB->getDependence();
  if (auto *Len = E->getLength())
    D |= Len->getDependence();
  return D;
}

ExprDependence latino::computeDependence(OMPArrayShapingExpr *E) {
  auto D = E->getBase()->getDependence() |
           toExprDependence(E->getType()->getDependence());
  for (Expr *Dim: E->getDimensions())
    if (Dim)
      D |= Dim->getDependence();
  return D;
}

ExprDependence latino::computeDependence(OMPIteratorExpr *E) {
  auto D = toExprDependence(E->getType()->getDependence());
  for (unsigned I = 0, End = E->numOfIterators(); I < End; ++I) {
    if (auto *VD = cast_or_null<ValueDecl>(E->getIteratorDecl(I)))
      D |= toExprDependence(VD->getType()->getDependence());
    OMPIteratorExpr::IteratorRange IR = E->getIteratorRange(I);
    if (Expr *BE = IR.Begin)
      D |= BE->getDependence();
    if (Expr *EE = IR.End)
      D |= EE->getDependence();
    if (Expr *SE = IR.Step)
      D |= SE->getDependence();
  }
  return D;
}

/// Compute the type-, value-, and instantiation-dependence of a
/// declaration reference
/// based on the declaration being referenced.
ExprDependence latino::computeDependence(DeclRefExpr *E, const ASTContext &Ctx) {
  auto Deps = ExprDependence::None;

  if (auto *NNS = E->getQualifier())
    Deps |= toExprDependence(NNS->getDependence() &
                             ~NestedNameSpecifierDependence::Dependent);

  if (auto *FirstArg = E->getTemplateArgs()) {
    unsigned NumArgs = E->getNumTemplateArgs();
    for (auto *Arg = FirstArg, *End = FirstArg + NumArgs; Arg < End; ++Arg)
      Deps |= toExprDependence(Arg->getArgument().getDependence());
  }

  auto *Decl = E->getDecl();
  auto Type = E->getType();

  if (Decl->isParameterPack())
    Deps |= ExprDependence::UnexpandedPack;
  Deps |= toExprDependence(Type->getDependence()) & ExprDependence::Error;

  // (TD) C++ [temp.dep.expr]p3:
  //   An id-expression is type-dependent if it contains:
  //
  // and
  //
  // (VD) C++ [temp.dep.constexpr]p2:
  //  An identifier is value-dependent if it is:

  //  (TD)  - an identifier that was declared with dependent type
  //  (VD)  - a name declared with a dependent type,
  if (Type->isDependentType())
    return Deps | ExprDependence::TypeValueInstantiation;
  else if (Type->isInstantiationDependentType())
    Deps |= ExprDependence::Instantiation;

  //  (TD)  - a conversion-function-id that specifies a dependent type
  if (Decl->getDeclName().getNameKind() ==
      DeclarationName::CXXConversionFunctionName) {
    QualType T = Decl->getDeclName().getCXXNameType();
    if (T->isDependentType())
      return Deps | ExprDependence::TypeValueInstantiation;

    if (T->isInstantiationDependentType())
      Deps |= ExprDependence::Instantiation;
  }

  //  (VD)  - the name of a non-type template parameter,
  if (isa<NonTypeTemplateParmDecl>(Decl))
    return Deps | ExprDependence::ValueInstantiation;

  //  (VD) - a constant with integral or enumeration type and is
  //         initialized with an expression that is value-dependent.
  //  (VD) - a constant with literal type and is initialized with an
  //         expression that is value-dependent [C++11].
  //  (VD) - FIXME: Missing from the standard:
  //       -  an entity with reference type and is initialized with an
  //          expression that is value-dependent [C++11]
  if (VarDecl *Var = dyn_cast<VarDecl>(Decl)) {
    if ((Ctx.getLangOpts().CPlusPlus11
             ? Var->getType()->isLiteralType(Ctx)
             : Var->getType()->isIntegralOrEnumerationType()) &&
        (Var->getType().isConstQualified() ||
         Var->getType()->isReferenceType())) {
      if (const Expr *Init = Var->getAnyInitializer())
        if (Init->isValueDependent()) {
          Deps |= ExprDependence::ValueInstantiation;
        }
    }

    // (VD) - FIXME: Missing from the standard:
    //      -  a member function or a static data member of the current
    //         instantiation
    if (Var->isStaticDataMember() &&
        Var->getDeclContext()->isDependentContext()) {
      Deps |= ExprDependence::ValueInstantiation;
      TypeSourceInfo *TInfo = Var->getFirstDecl()->getTypeSourceInfo();
      if (TInfo->getType()->isIncompleteArrayType())
        Deps |= ExprDependence::Type;
    }

    return Deps;
  }

  // (VD) - FIXME: Missing from the standard:
  //      -  a member function or a static data member of the current
  //         instantiation
  if (isa<CXXMethodDecl>(Decl) && Decl->getDeclContext()->isDependentContext())
    Deps |= ExprDependence::ValueInstantiation;
  return Deps;
}

ExprDependence latino::computeDependence(RecoveryExpr *E) {
  // RecoveryExpr is
  //   - always value-dependent, and therefore instantiation dependent
  //   - contains errors (ExprDependence::Error), by definition
  //   - type-dependent if we don't know the type (fallback to an opaque
  //     dependent type), or the type is known and dependent, or it has
  //     type-dependent subexpressions.
  auto D = toExprDependence(E->getType()->getDependence()) |
           ExprDependence::ValueInstantiation | ExprDependence::Error;
  // FIXME: remove the type-dependent bit from subexpressions, if the
  // RecoveryExpr has a non-dependent type.
  for (auto *S : E->subExpressions())
    D |= S->getDependence();
  return D;
}

ExprDependence latino::computeDependence(PredefinedExpr *E) {
  return toExprDependence(E->getType()->getDependence()) &
         ~ExprDependence::UnexpandedPack;
}

ExprDependence latino::computeDependence(CallExpr *E,
                                        llvm::ArrayRef<Expr *> PreArgs) {
  auto D = E->getCallee()->getDependence();
  for (auto *A : llvm::makeArrayRef(E->getArgs(), E->getNumArgs())) {
    if (A)
      D |= A->getDependence();
  }
  for (auto *A : PreArgs)
    D |= A->getDependence();
  return D;
}

ExprDependence latino::computeDependence(OffsetOfExpr *E) {
  auto D = turnTypeToValueDependence(
      toExprDependence(E->getTypeSourceInfo()->getType()->getDependence()));
  for (unsigned I = 0, N = E->getNumExpressions(); I < N; ++I)
    D |= turnTypeToValueDependence(E->getIndexExpr(I)->getDependence());
  return D;
}

ExprDependence latino::computeDependence(MemberExpr *E) {
  auto *MemberDecl = E->getMemberDecl();
  auto D = E->getBase()->getDependence();
  if (FieldDecl *FD = dyn_cast<FieldDecl>(MemberDecl)) {
    DeclContext *DC = MemberDecl->getDeclContext();
    // dyn_cast_or_null is used to handle objC variables which do not
    // have a declaration context.
    CXXRecordDecl *RD = dyn_cast_or_null<CXXRecordDecl>(DC);
    if (RD && RD->isDependentContext() && RD->isCurrentInstantiation(DC)) {
      if (!E->getType()->isDependentType())
        D &= ~ExprDependence::Type;
    }

    // Bitfield with value-dependent width is type-dependent.
    if (FD && FD->isBitField() && FD->getBitWidth()->isValueDependent()) {
      D |= ExprDependence::Type;
    }
  }
  // FIXME: move remaining dependence computation from MemberExpr::Create()
  return D;
}

ExprDependence latino::computeDependence(InitListExpr *E) {
  auto D = ExprDependence::None;
  for (auto *A : E->inits())
    D |= A->getDependence();
  return D;
}

ExprDependence latino::computeDependence(ShuffleVectorExpr *E) {
  auto D = toExprDependence(E->getType()->getDependence());
  for (auto *C : llvm::makeArrayRef(E->getSubExprs(), E->getNumSubExprs()))
    D |= C->getDependence();
  return D;
}

ExprDependence latino::computeDependence(GenericSelectionExpr *E,
                                        bool ContainsUnexpandedPack) {
  auto D = ContainsUnexpandedPack ? ExprDependence::UnexpandedPack
                                  : ExprDependence::None;
  for (auto *AE : E->getAssocExprs())
    D |= AE->getDependence() & ExprDependence::Error;
  D |= E->getControllingExpr()->getDependence() & ExprDependence::Error;

  if (E->isResultDependent())
    return D | ExprDependence::TypeValueInstantiation;
  return D | (E->getResultExpr()->getDependence() &
              ~ExprDependence::UnexpandedPack);
}

ExprDependence latino::computeDependence(DesignatedInitExpr *E) {
  auto Deps = E->getInit()->getDependence();
  for (auto D : E->designators()) {
    auto DesignatorDeps = ExprDependence::None;
    if (D.isArrayDesignator())
      DesignatorDeps |= E->getArrayIndex(D)->getDependence();
    else if (D.isArrayRangeDesignator())
      DesignatorDeps |= E->getArrayRangeStart(D)->getDependence() |
                        E->getArrayRangeEnd(D)->getDependence();
    Deps |= DesignatorDeps;
    if (DesignatorDeps & ExprDependence::TypeValue)
      Deps |= ExprDependence::TypeValueInstantiation;
  }
  return Deps;
}

ExprDependence latino::computeDependence(PseudoObjectExpr *O) {
  auto D = O->getSyntacticForm()->getDependence();
  for (auto *E : O->semantics())
    D |= E->getDependence();
  return D;
}

ExprDependence latino::computeDependence(AtomicExpr *A) {
  auto D = ExprDependence::None;
  for (auto *E : llvm::makeArrayRef(A->getSubExprs(), A->getNumSubExprs()))
    D |= E->getDependence();
  return D;
}

ExprDependence latino::computeDependence(CXXNewExpr *E) {
  auto D = toExprDependence(E->getType()->getDependence());
  auto Size = E->getArraySize();
  if (Size.hasValue() && *Size)
    D |= turnTypeToValueDependence((*Size)->getDependence());
  if (auto *I = E->getInitializer())
    D |= turnTypeToValueDependence(I->getDependence());
  for (auto *A : E->placement_arguments())
    D |= turnTypeToValueDependence(A->getDependence());
  return D;
}

ExprDependence latino::computeDependence(CXXPseudoDestructorExpr *E) {
  auto D = E->getBase()->getDependence();
  if (!E->getDestroyedType().isNull())
    D |= toExprDependence(E->getDestroyedType()->getDependence());
  if (auto *ST = E->getScopeTypeInfo())
    D |= turnTypeToValueDependence(
        toExprDependence(ST->getType()->getDependence()));
  if (auto *Q = E->getQualifier())
    D |= toExprDependence(Q->getDependence() &
                          ~NestedNameSpecifierDependence::Dependent);
  return D;
}

static inline ExprDependence getDependenceInExpr(DeclarationNameInfo Name) {
  auto D = ExprDependence::None;
  if (Name.isInstantiationDependent())
    D |= ExprDependence::Instantiation;
  if (Name.containsUnexpandedParameterPack())
    D |= ExprDependence::UnexpandedPack;
  return D;
}

ExprDependence
latino::computeDependence(OverloadExpr *E, bool KnownDependent,
                         bool KnownInstantiationDependent,
                         bool KnownContainsUnexpandedParameterPack) {
  auto Deps = ExprDependence::None;
  if (KnownDependent)
    Deps |= ExprDependence::TypeValue;
  if (KnownInstantiationDependent)
    Deps |= ExprDependence::Instantiation;
  if (KnownContainsUnexpandedParameterPack)
    Deps |= ExprDependence::UnexpandedPack;
  Deps |= getDependenceInExpr(E->getNameInfo());
  if (auto *Q = E->getQualifier())
    Deps |= toExprDependence(Q->getDependence() &
                             ~NestedNameSpecifierDependence::Dependent);
  for (auto *D : E->decls()) {
    if (D->getDeclContext()->isDependentContext() ||
        isa<UnresolvedUsingValueDecl>(D))
      Deps |= ExprDependence::TypeValueInstantiation;
  }
  // If we have explicit template arguments, check for dependent
  // template arguments and whether they contain any unexpanded pack
  // expansions.
  for (auto A : E->template_arguments())
    Deps |= toExprDependence(A.getArgument().getDependence());
  return Deps;
}

ExprDependence latino::computeDependence(DependentScopeDeclRefExpr *E) {
  auto D = ExprDependence::TypeValue;
  D |= getDependenceInExpr(E->getNameInfo());
  if (auto *Q = E->getQualifier())
    D |= toExprDependence(Q->getDependence());
  for (auto A : E->template_arguments())
    D |= toExprDependence(A.getArgument().getDependence());
  return D;
}

ExprDependence latino::computeDependence(CXXConstructExpr *E) {
  auto D = toExprDependence(E->getType()->getDependence());
  for (auto *A : E->arguments())
    D |= A->getDependence() & ~ExprDependence::Type;
  return D;
}

ExprDependence latino::computeDependence(LambdaExpr *E,
                                        bool ContainsUnexpandedParameterPack) {
  auto D = toExprDependence(E->getType()->getDependence());
  if (ContainsUnexpandedParameterPack)
    D |= ExprDependence::UnexpandedPack;
  return D;
}

ExprDependence latino::computeDependence(CXXUnresolvedConstructExpr *E) {
  auto D = ExprDependence::ValueInstantiation;
  D |= toExprDependence(E->getType()->getDependence());
  if (E->getType()->getContainedDeducedType())
    D |= ExprDependence::Type;
  for (auto *A : E->arguments())
    D |= A->getDependence() &
         (ExprDependence::UnexpandedPack | ExprDependence::Error);
  return D;
}

ExprDependence latino::computeDependence(CXXDependentScopeMemberExpr *E) {
  auto D = ExprDependence::TypeValueInstantiation;
  if (!E->isImplicitAccess())
    D |= E->getBase()->getDependence();
  if (auto *Q = E->getQualifier())
    D |= toExprDependence(Q->getDependence());
  D |= getDependenceInExpr(E->getMemberNameInfo());
  for (auto A : E->template_arguments())
    D |= toExprDependence(A.getArgument().getDependence());
  return D;
}

ExprDependence latino::computeDependence(MaterializeTemporaryExpr *E) {
  return E->getSubExpr()->getDependence();
}

ExprDependence latino::computeDependence(CXXFoldExpr *E) {
  auto D = ExprDependence::TypeValueInstantiation;
  for (const auto *C : {E->getLHS(), E->getRHS()}) {
    if (C)
      D |= C->getDependence() & ~ExprDependence::UnexpandedPack;
  }
  return D;
}

ExprDependence latino::computeDependence(TypeTraitExpr *E) {
  auto D = ExprDependence::None;
  for (const auto *A : E->getArgs())
    D |=
        toExprDependence(A->getType()->getDependence()) & ~ExprDependence::Type;
  return D;
}

ExprDependence latino::computeDependence(ConceptSpecializationExpr *E,
                                        bool ValueDependent) {
  auto TA = TemplateArgumentDependence::None;
  const auto InterestingDeps = TemplateArgumentDependence::Instantiation |
                               TemplateArgumentDependence::UnexpandedPack;
  for (const TemplateArgumentLoc &ArgLoc :
       E->getTemplateArgsAsWritten()->arguments()) {
    TA |= ArgLoc.getArgument().getDependence() & InterestingDeps;
    if (TA == InterestingDeps)
      break;
  }

  ExprDependence D =
      ValueDependent ? ExprDependence::Value : ExprDependence::None;
  return D | toExprDependence(TA);
}

// ExprDependence latino::computeDependence(ObjCArrayLiteral *E) {
//   auto D = ExprDependence::None;
//   Expr **Elements = E->getElements();
//   for (unsigned I = 0, N = E->getNumElements(); I != N; ++I)
//     D |= turnTypeToValueDependence(Elements[I]->getDependence());
//   return D;
// }

// ExprDependence latino::computeDependence(ObjCDictionaryLiteral *E) {
//   auto Deps = ExprDependence::None;
//   for (unsigned I = 0, N = E->getNumElements(); I < N; ++I) {
//     auto KV = E->getKeyValueElement(I);
//     auto KVDeps = turnTypeToValueDependence(KV.Key->getDependence() |
//                                             KV.Value->getDependence());
//     if (KV.EllipsisLoc.isValid())
//       KVDeps &= ~ExprDependence::UnexpandedPack;
//     Deps |= KVDeps;
//   }
//   return Deps;
// }

// ExprDependence latino::computeDependence(ObjCMessageExpr *E) {
//   auto D = ExprDependence::None;
//   if (auto *R = E->getInstanceReceiver())
//     D |= R->getDependence();
//   else
//     D |= toExprDependence(E->getType()->getDependence());
//   for (auto *A : E->arguments())
//     D |= A->getDependence();
//   return D;
// }
