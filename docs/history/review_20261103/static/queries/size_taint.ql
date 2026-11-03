/**
 * @name File or stream data reaches a size, length, index or loop bound with no upper-bound check
 * @description Values read from a file, stream or serializer (IO::Stream::Read and overrides,
 *              ReaderWriter / Serializer Serialize* out-parameters, FileSystem::ReadFile, Win32
 *              ReadFile, fread) flow to an allocation size, an array SetSize / reserve count, a
 *              memcpy / memmove / read length, an array index or a loop bound, with no guard that
 *              bounds the value from above on the way. Review campaign 2026-11-03, phase P3.
 * @kind path-problem
 * @problem.severity warning
 * @id openba/size-taint
 * @tags security external/cwe/cwe-789 external/cwe/cwe-120 external/cwe/cwe-129 external/cwe/cwe-606
 */

import cpp
import semmle.code.cpp.dataflow.new.TaintTracking
import semmle.code.cpp.controlflow.Guards
import semmle.code.cpp.valuenumbering.GlobalValueNumbering
import SizeFlow::PathGraph

/** A scalar the serializer reads into: `T&` where T is arithmetic or an enum. */
predicate isScalarOut(Parameter p) {
  exists(Type t |
    t = p.getType().(ReferenceType).getBaseType() and
    not t.isConst() and
    (
      t.getUnspecifiedType() instanceof ArithmeticType or
      t.getUnspecifiedType() instanceof Enum
    )
  )
}

/** `c`'s argument `i` receives bytes or a value read from untrusted input. */
predicate readsInto(Call c, int i) {
  exists(Function f | f = c.getTarget() |
    // IO::Stream::Read(int n, void* dst) and every override (MemoryStream, FileStream,
    // CompressedStream, SegmentStream, ...). Matched by shape: the build-mode-none database
    // does not always resolve the override set.
    f.getName() = "Read" and
    f.getNumberOfParameters() = 2 and
    f.getParameter(1).getType().getUnspecifiedType() instanceof PointerType and
    i = 1
    or
    // StreamCache::Record::Read(pos, n, dst)
    f.getName() = "Read" and
    f.getNumberOfParameters() = 3 and
    f.getParameter(2).getType().getUnspecifiedType() instanceof PointerType and
    i = 2
    or
    // IO::FileSystem::ReadFile(FileInfo&, int n, void* dst)
    f.getName() = "ReadFile" and f.getNumberOfParameters() = 3 and i = 2
    or
    // Win32 ReadFile(h, buf, n, &read, ov)
    f.getName() = "ReadFile" and f.getNumberOfParameters() = 5 and i = 1
    or
    f.hasGlobalOrStdName("fread") and i = 0
    or
    // Serializer / ReaderWriter / BinaryReader scalar forms: Serialize(T& v, name),
    // SerializeInt(int& v, name), ... (the write path is included: same call, harmless)
    f.getName().regexpMatch("Serialize[A-Za-z0-9]*") and
    isScalarOut(f.getParameter(0)) and
    i = 0
    or
    // array forms: Serialize(int n, T* d, name), SerializeIntArray(int n, int* d, name),
    // SerializeBytes(int n, void* buf, name)
    f.getName().regexpMatch("Serialize[A-Za-z0-9]*") and
    f.getNumberOfParameters() = 3 and
    f.getParameter(1).getType().getUnspecifiedType() instanceof PointerType and
    i = 1
  )
}

/** The sink kinds, used for ranking in the triage. */
predicate sinkKind(Expr e, string kind) {
  // allocation sizes
  exists(FunctionCall fc, string n | n = fc.getTarget().getName() |
    n = ["MemAlloc", "RtMemAlloc", "malloc", "_aligned_malloc", "_malloca", "alloca", "_alloca"] and
    e = fc.getArgument(0) and
    kind = "alloc-size"
    or
    n = "calloc" and e = fc.getArgument([0, 1]) and kind = "alloc-size"
    or
    n = ["realloc", "RtMemRealloc", "MemRealloc"] and e = fc.getArgument(1) and kind = "alloc-size"
    or
    n = ["operator new", "operator new[]"] and e = fc.getArgument(0) and kind = "alloc-size"
  )
  or
  exists(NewArrayExpr na | e = na.getExtent() and kind = "alloc-size")
  or
  // collection counts: Array::SetSize(n), SetSizeFill(n, fill), std reserve / resize,
  // String::SetLength(n), and the free forms RtColl_ArraySetSize_<addr>(arr, n),
  // Rt_SetSize_<addr>(arr, n), RawArraySetSize(arr, n, ...), *ArraySetSize(...): the count is the
  // first integral parameter.
  exists(FunctionCall fc, Function f, int i | f = fc.getTarget() |
    f.getName()
        .regexpMatch("(?i).*(SetSize|SetSizeFill|reserve|resize|SetLength|SetCapacity)(_[0-9a-f]+)?") and
    not f.getName() = "SetSizeText" and
    f.getParameter(i).getType().getUnspecifiedType() instanceof IntegralType and
    not exists(int j | j < i and f.getParameter(j).getType().getUnspecifiedType() instanceof IntegralType) and
    e = fc.getArgument(i) and
    kind = "count"
  )
  or
  // copy lengths
  exists(FunctionCall fc, string n | n = fc.getTarget().getName() |
    n = ["memcpy", "memmove", "memset", "__builtin_memcpy", "__builtin_memmove", "strncpy"] and
    e = fc.getArgument(2) and
    kind = "copy-len"
    or
    n = ["memcpy_s", "memmove_s"] and e = fc.getArgument(3) and kind = "copy-len"
  )
  or
  // a read length into a caller buffer: Stream::Read(n, dst), SerializeBytes(n, buf), arrays
  exists(Call c, int i |
    readsInto(c, i) and
    c.getTarget().getNumberOfParameters() in [2, 3] and
    not isScalarOut(c.getTarget().getParameter(0)) and
    i > 0 and
    e = c.getArgument(i - 1) and
    c.getTarget().getParameter(i - 1).getType().getUnspecifiedType() instanceof IntegralType and
    kind = "read-len"
  )
  or
  // array indexes and pointer offsets
  exists(ArrayExpr ae | e = ae.getArrayOffset() and kind = "index")
  or
  exists(PointerAddExpr pa | e = pa.getRightOperand() and kind = "ptr-offset")
  or
  // loop bounds
  exists(Loop l, RelationalOperation r |
    r = l.getCondition().getAChild*() and
    e = r.getAnOperand() and
    kind = "loop-bound"
  )
}

/** `e` sits in a block where a guard bounds the same value from above. */
predicate upperBounded(Expr e) {
  exists(GuardCondition g, Expr bounded, BasicBlock bb |
    bb = e.getBasicBlock() and
    (
      g.ensuresLt(bounded, _, _, bb, true)
      or
      g.ensuresLt(_, bounded, _, bb, false)
    ) and
    globalValueNumber(bounded.getUnconverted()) = globalValueNumber(e.getUnconverted())
  )
}

module SizeConfig implements DataFlow::ConfigSig {
  predicate isSource(DataFlow::Node n) {
    exists(Call c, int i | readsInto(c, i) and n.asDefiningArgument() = c.getArgument(i))
  }

  predicate isSink(DataFlow::Node n) { sinkKind(n.asExpr(), _) }

  predicate isBarrier(DataFlow::Node n) {
    upperBounded(n.asExpr())
    or
    // bool and char-sized values cannot drive a large size; a u8 index still reports
    // through the index arm unless it is bool
    n.asExpr().getUnspecifiedType() instanceof BoolType
  }

  predicate isBarrierOut(DataFlow::Node n) { isSink(n) }
}

module SizeFlow = TaintTracking::Global<SizeConfig>;

from SizeFlow::PathNode source, SizeFlow::PathNode sink, string kind
where
  SizeFlow::flowPath(source, sink) and
  sinkKind(sink.getNode().asExpr(), kind)
select sink.getNode(), source, sink,
  "[" + kind + "] unbounded value from $@ reaches " + sink.getNode().asExpr().toString() + " in " +
    sink.getNode().getFunction().getQualifiedName(), source.getNode(),
  source.getNode().toString()
