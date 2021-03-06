// Copyright (c) 2004-2012, Cray Inc.  (See LICENSE file for more details)


// DefaultRectangular.chpl
//
pragma "no use ChapelStandard"
module DefaultRectangular {

use DSIUtil;
config param debugDefaultDist = false;
config param debugDefaultDistBulkTransfer = false;
config param debugDataPar = false;

config param defaultDoRADOpt = true;
config param defaultDisableLazyRADOpt = false;

class DefaultDist: BaseDist {
  proc dsiNewRectangularDom(param rank: int, type idxType, param stridable: bool)
    return new DefaultRectangularDom(rank, idxType, stridable, this);

  proc dsiNewAssociativeDom(type idxType, param parSafe: bool)
    return new DefaultAssociativeDom(idxType, parSafe, this);

  proc dsiNewOpaqueDom(type idxType, param parSafe: bool)
    return new DefaultOpaqueDom(this, parSafe);

  proc dsiNewSparseDom(param rank: int, type idxType, dom: domain)
    return new DefaultSparseDom(rank, idxType, this, dom);

  proc dsiIndexToLocale(ind) return this.locale;

  proc dsiClone() return this;

  proc dsiAssign(other: this.type) { }

  proc dsiCreateReindexDist(newSpace, oldSpace) return this;
  proc dsiCreateRankChangeDist(param newRank, args) return this;
}

//
// Note that the replicated copies are set up in ChapelLocale on the
// other locales.  This just sets it up on this locale.
//
pragma "private" var defaultDist = new dmap(new DefaultDist());

class DefaultRectangularDom: BaseRectangularDom {
  param rank : int;
  type idxType;
  param stridable: bool;
  var dist: DefaultDist;
  var ranges : rank*range(idxType,BoundedRangeType.bounded,stridable);

  proc linksDistribution() param return false;
  proc dsiLinksDistribution()     return false;

  proc DefaultRectangularDom(param rank, type idxType, param stridable, dist) {
    this.dist = dist;
  }

  proc dsiDisplayRepresentation() {
    writeln("ranges = ", ranges);
  }

  proc dsiClear() {
    var emptyRange: range(idxType, BoundedRangeType.bounded, stridable);
    for param i in 1..rank do
      ranges(i) = emptyRange;
  }
  
  // function and iterator versions, also for setIndices
  proc dsiGetIndices() return ranges;

  proc dsiSetIndices(x) {
    if ranges.size != x.size then
      compilerError("rank mismatch in domain assignment");
    if ranges(1).idxType != x(1).idxType then
      compilerError("index type mismatch in domain assignment");
    ranges = x;
  }

  iter these_help(param d: int) {
    if d == rank - 1 {
      for i in ranges(d) do
        for j in ranges(rank) do
          yield (i, j);
    } else {
      for i in ranges(d) do
        for j in these_help(d+1) do
          yield (i, (...j));
    }
  }

  iter these_help(param d: int, block) {
    if d == block.size - 1 {
      for i in block(d) do
        for j in block(block.size) do
          yield (i, j);
    } else {
      for i in block(d) do
        for j in these_help(d+1, block) do
          yield (i, (...j));
    }
  }

  iter these() {
    if rank == 1 {
      for i in ranges(1) do
        yield i;
    } else {
      for i in these_help(1) do
        yield i;
    }
  }

  iter these(param tag: iterKind) where tag == iterKind.leader {
    if debugDefaultDist then
      writeln("*** In domain/array leader code:"); // this = ", this);
    const numTasks = if dataParTasksPerLocale==0 then here.numCores
                     else dataParTasksPerLocale;
    const ignoreRunning = dataParIgnoreRunningTasks;
    const minIndicesPerTask = dataParMinGranularity;
    if debugDataPar {
      writeln("### numTasks = ", numTasks);
      writeln("### ignoreRunning = ", ignoreRunning);
      writeln("### minIndicesPerTask = ", minIndicesPerTask);
    }

    if debugDefaultDist then
      writeln("    numTasks=", numTasks, " (", ignoreRunning,
              "), minIndicesPerTask=", minIndicesPerTask);

    var (numChunks, parDim) = _computeChunkStuff(numTasks, ignoreRunning,
                                                 minIndicesPerTask, ranges);
    if debugDefaultDist then
      writeln("    numChunks=", numChunks, " parDim=", parDim,
              " ranges(", parDim, ").length=", ranges(parDim).length);

    if debugDataPar then writeln("### numChunks=", numChunks, " (parDim=", parDim, ")");

    if (CHPL_TARGET_PLATFORM != "xmt") {
      if numChunks == 1 {
        if rank == 1 {
          yield tuple(0..ranges(1).length-1);
        } else {
          var block: rank*range(idxType);
          for param i in 1..rank do
            block(i) = 0..ranges(i).length-1;
          yield block;
        }
      } else {
        var locBlock: rank*range(idxType);
        for param i in 1..rank do
          locBlock(i) = 0:ranges(i).low.type..#(ranges(i).length);
        if debugDefaultDist then
          writeln("*** DI: locBlock = ", locBlock);
        coforall chunk in 0..#numChunks {
          var tuple: rank*range(idxType) = locBlock;
          const (lo,hi) = _computeBlock(locBlock(parDim).length,
                                        numChunks, chunk,
                                        locBlock(parDim).high);
          tuple(parDim) = lo..hi;
          if debugDefaultDist then
            writeln("*** DI[", chunk, "]: tuple = ", tuple);
          yield tuple;
        }
      }
    } else {

      var per_stream_i: uint(64) = 0;
      var total_streams_n: uint(64) = 0;

      var locBlock: rank*range(idxType);
      for param i in 1..rank do
        locBlock(i) = 0:ranges(i).low.type..#(ranges(i).length);

      __primitive_loop("xmt pragma forall i in n", per_stream_i,
                       total_streams_n) {

        var tuple: rank*range(idxType) = locBlock;
        const (lo,hi) = _computeBlock(ranges(parDim).length,
                                      total_streams_n, per_stream_i,
                                      (ranges(parDim).length-1));
        tuple(parDim) = lo..hi;
        yield tuple;
      }
    }
  }

  iter these(param tag: iterKind, followThis) where tag == iterKind.follower {
    proc anyStridable(rangeTuple, param i: int = 1) param
      return if i == rangeTuple.size then rangeTuple(i).stridable
             else rangeTuple(i).stridable || anyStridable(rangeTuple, i+1);

    chpl__testPar("default rectangular domain follower invoked on ", followThis);
    if debugDefaultDist then
      writeln("In domain follower code: Following ", followThis);
    param stridable = this.stridable || anyStridable(followThis);
    var block: rank*range(idxType=idxType, stridable=stridable);
    if stridable {
      for param i in 1..rank {
        const rStride = ranges(i).stride:idxType,
              fStride = followThis(i).stride:idxType;
        if ranges(i).stride > 0 {
          const low = ranges(i).low + followThis(i).low*rStride,
                high = ranges(i).low + followThis(i).high*rStride,
                stride = (rStride * fStride):int;
          block(i) = low..high by stride;
        } else {
          const low = ranges(i).high + followThis(i).high*rStride,
                high = ranges(i).high + followThis(i).low*rStride,
                stride = (rStride * fStride): int;
          block(i) = low..high by stride;
        }
      }
    } else {
      for  param i in 1..rank do
        block(i) = ranges(i).low+followThis(i).low:idxType..ranges(i).low+followThis(i).high:idxType;
    }

    if rank == 1 {
      for i in zip((...block)) {
        __primitive("noalias pragma");
        yield i;
      }
    } else {
      for i in these_help(1, block) {
        __primitive("noalias pragma");
        yield i;
      }
    }
  }

  proc dsiMember(ind: rank*idxType) {
    for param i in 1..rank do
      if !ranges(i).member(ind(i)) then
        return false;
    return true;
  }

  proc dsiIndexOrder(ind: rank*idxType) {
    var totOrder: idxType;
    var blk: idxType = 1;
    for param d in 1..rank by -1 {
      const orderD = ranges(d).indexOrder(ind(d));
      if (orderD == -1) then return orderD;
      totOrder += orderD * blk;
      blk *= ranges(d).length;
    }
    return totOrder;
  }

  proc dsiDims()
    return ranges;

  proc dsiDim(d : int)
    return ranges(d);

  // optional, is this necesary? probably not now that
  // homogeneous tuples are implemented as C vectors.
  proc dsiDim(param d : int)
    return ranges(d);

  proc dsiNumIndices {
    var sum = 1:idxType;
    for param i in 1..rank do
      sum *= ranges(i).length;
    return sum;
    // WANT: return * reduce (this(1..rank).length);
  }

  proc dsiLow {
    if rank == 1 {
      return ranges(1).low;
    } else {
      var result: rank*idxType;
      for param i in 1..rank do
        result(i) = ranges(i).low;
      return result;
    }
  }

  proc dsiHigh {
    if rank == 1 {
      return ranges(1).high;
    } else {
      var result: rank*idxType;
      for param i in 1..rank do
        result(i) = ranges(i).high;
      return result;
    }
  }

  proc dsiAlignedLow {
    if rank == 1 {
      return ranges(1).alignedLow;
    } else {
      var result: rank*idxType;
      for param i in 1..rank do
        result(i) = ranges(i).alignedLow;
      return result;
    }
  }

  proc dsiAlignedHigh {
    if rank == 1 {
      return ranges(1).alignedHigh;
    } else {
      var result: rank*idxType;
      for param i in 1..rank do
        result(i) = ranges(i).alignedHigh;
      return result;
    }
  }

  proc dsiStride {
    if rank == 1 {
      return ranges(1).stride;
    } else {
      var result: rank*chpl__signedType(idxType);
      for param i in 1..rank do
        result(i) = ranges(i).stride;
      return result;
    }
  }

  proc dsiAlignment {
    if rank == 1 {
      return ranges(1).alignment;
    } else {
      var result: rank*idxType;
      for param i in 1..rank do
        result(i) = ranges(i).alignment;
      return result;
    }
  }

  proc dsiFirst {
    if rank == 1 {
      return ranges(1).first;
    } else {
      var result: rank*idxType;
      for param i in 1..rank do
        result(i) = ranges(i).first;
      return result;
    }
  }

  proc dsiLast {
    if rank == 1 {
      return ranges(1).last;
    } else {
      var result: rank*idxType;
      for param i in 1..rank do
        result(i) = ranges(i).last;
      return result;
    }
  }

  proc dsiBuildArray(type eltType) {
    return new DefaultRectangularArr(eltType=eltType, rank=rank, idxType=idxType,
                                    stridable=stridable, dom=this);
  }

  proc dsiBuildRectangularDom(param rank: int, type idxType, param stridable: bool,
                            ranges: rank*range(idxType,
                                               BoundedRangeType.bounded,
                                               stridable)) {
    var dom = new DefaultRectangularDom(rank, idxType, stridable, dist);
    for i in 1..rank do
      dom.ranges(i) = ranges(i);
    return dom;
  }
}

record _remoteAccessData {
  type eltType;
  param rank : int;
  type idxType;
  var off: rank*idxType;
  var blk: rank*idxType;
  var str: rank*chpl__signedType(idxType);
  var origin: idxType;
  var factoredOffs: idxType;
  var data: _ddata(eltType);
}

//
// Local cache of remote ddata access info
//
class LocRADCache {
  type eltType;
  param rank: int;
  type idxType;
  var targetLocDom: domain(rank);
  var RAD: [targetLocDom] _remoteAccessData(eltType, rank, idxType);

  proc LocRADCache(type eltType, param rank: int, type idxType,
                   newTargetLocDom: domain(rank)) {
    // This should resize the arrays
    targetLocDom=newTargetLocDom;
  }
}

class DefaultRectangularArr: BaseArr {
  type eltType;
  param rank : int;
  type idxType;
  param stridable: bool;

  var dom : DefaultRectangularDom(rank=rank, idxType=idxType,
                                         stridable=stridable);
  var off: rank*idxType;
  var blk: rank*idxType;
  var str: rank*chpl__signedType(idxType);
  var origin: idxType;
  var factoredOffs: idxType;
  var data : _ddata(eltType);
  var noinit: bool = false;
  //var numelm: int = -1; // for correctness checking

  proc canCopyFromDevice param return true;

  // end class definition here, then defined secondary methods below

  proc dsiDisplayRepresentation() {
    writeln("off=", off);
    writeln("blk=", blk);
    writeln("str=", str);
    writeln("origin=", origin);
    writeln("factoredOffs=", factoredOffs);
    writeln("noinit=", noinit);
  }

  // can the compiler create this automatically?
  proc dsiGetBaseDom() return dom;

  proc dsiDestroyData() {
    if dom.dsiNumIndices > 0 {
      pragma "no copy" pragma "no auto destroy" var dr = data;
      pragma "no copy" pragma "no auto destroy" var dv = __primitive("deref", dr);
      pragma "no copy" pragma "no auto destroy" var er = __primitive("array_get", dv, 0);
      pragma "no copy" pragma "no auto destroy" var ev = __primitive("deref", er);
      if (chpl__maybeAutoDestroyed(ev)) {
        for i in 0..dom.dsiNumIndices-1 {
          pragma "no copy" pragma "no auto destroy" var dr = data;
          pragma "no copy" pragma "no auto destroy" var dv = __primitive("deref", dr);
          pragma "no copy" pragma "no auto destroy" var er = __primitive("array_get", dv, i);
          pragma "no copy" pragma "no auto destroy" var ev = __primitive("deref", er);
          chpl__autoDestroy(ev);
        }
      }
    }
    _ddata_free(data);
  }

  iter these() var {
    if rank == 1 {
      // This is specialized to avoid overheads of calling dsiAccess()
      if !dom.stridable {
        // This is specialized because the strided version disables the
        // "single loop iterator" optimization
        var first = getDataIndex(dom.dsiLow);
        var second = getDataIndex(dom.dsiLow+dom.ranges(1).stride:idxType);
        var step = (second-first):chpl__signedType(idxType);
        var last = first + (dom.dsiNumIndices-1) * step:idxType;
        for i in first..last by step do
          yield data(i);
      } else {
        const stride = dom.ranges(1).stride: idxType,
              start  = dom.ranges(1).first,
              first  = getDataIndex(start),
              second = getDataIndex(start + stride),
              step   = (second-first):chpl__signedType(idxType),
              last   = first + (dom.ranges(1).length-1) * step:idxType;
        if step > 0 then
          for i in first..last by step do
            yield data(i);
        else
          for i in last..first by step do
            yield data(i);
      }
    } else {
      for i in dom do
        yield dsiAccess(i);
    }
  }

  iter these(param tag: iterKind) where tag == iterKind.leader {
    for followThis in dom.these(tag) do
      yield followThis;
  }

  iter these(param tag: iterKind, followThis) var where tag == iterKind.follower {
    if debugDefaultDist then
      writeln("*** In array follower code:"); // [\n", this, "]");
    for i in dom.these(tag=iterKind.follower, followThis) {
      __primitive("noalias pragma");
      yield dsiAccess(i);
    }
  }

  proc computeFactoredOffs() {
    factoredOffs = 0:idxType;
    for param i in 1..rank do {
      factoredOffs = factoredOffs + blk(i) * off(i);
    }
  }
  
  // change name to setup and call after constructor call sites
  // we want to get rid of all initialize functions everywhere
  proc initialize() {
    if noinit == true then return;
    for param dim in 1..rank {
      off(dim) = dom.dsiDim(dim).alignedLow;
      str(dim) = dom.dsiDim(dim).stride;
    }
    blk(rank) = 1:idxType;
    for param dim in 1..(rank-1) by -1 do
      blk(dim) = blk(dim+1) * dom.dsiDim(dim+1).length;
    computeFactoredOffs();
    var size = blk(1) * dom.dsiDim(1).length;
    data = _ddata_allocate(eltType, size);
  }

  inline proc getDataIndex(ind: idxType ...1) where rank == 1
    return getDataIndex(ind);

  inline proc getDataIndex(ind: rank* idxType) {
    var sum = origin;
    if stridable {
      for param i in 1..rank do
        sum += (ind(i) - off(i)) * blk(i) / abs(str(i)):idxType;
    } else {
      for param i in 1..rank do
        sum += ind(i) * blk(i);
      sum -= factoredOffs;
    }
    return sum;
  }

  // only need second version because wrapper record can pass a 1-tuple
  inline proc dsiAccess(ind: idxType ...1) var where rank == 1
    return dsiAccess(ind);

  inline proc dsiAccess(ind : rank*idxType) var {
    if boundsChecking then
      if !dom.dsiMember(ind) then
        halt("array index out of bounds: ", ind);
    var dataInd = getDataIndex(ind);
    //assert(dataInd >= 0);
    //assert(numelm >= 0); // ensure it has been initialized
    //assert(dataInd: uint(64) < numelm: uint(64));
    return data(dataInd);
  }

  proc dsiReindex(d: DefaultRectangularDom) {
    var alias = new DefaultRectangularArr(eltType=eltType, rank=d.rank,
                                         idxType=d.idxType,
                                         stridable=d.stridable,
                                         dom=d, noinit=true);
    alias.data = data;
    //alias.numelm = numelm;
    //writeln("DR.dsiReindex blk: ", blk, " stride: ",dom.dsiDim(1).stride," str:",str(1));
    for param i in 1..rank {
      alias.off(i) = d.dsiDim(i).low;
      alias.blk(i) = (blk(i) * dom.dsiDim(i).stride / str(i)) : d.idxType;
      alias.str(i) = d.dsiDim(i).stride;
    }
    alias.origin = origin:d.idxType;
    alias.computeFactoredOffs();
    return alias;
  }

  proc dsiSlice(d: DefaultRectangularDom) {
    var alias = new DefaultRectangularArr(eltType=eltType, rank=rank,
                                         idxType=idxType,
                                         stridable=d.stridable,
                                         dom=d, noinit=true);
    alias.data = data;
    //alias.numelm = numelm;
    alias.blk = blk;
    alias.str = str;
    alias.origin = origin;
    for param i in 1..rank {
      alias.off(i) = d.dsiDim(i).low;
      alias.origin += blk(i) * (d.dsiDim(i).low - off(i)) / str(i);
    }
    alias.computeFactoredOffs();
    return alias;
  }

  proc dsiRankChange(d, param newRank: int, param newStridable: bool, args) {
    proc isRange(r: range(?e,?b,?s,?a)) param return 1;
    proc isRange(r) param return 0;

    var alias = new DefaultRectangularArr(eltType=eltType, rank=newRank,
                                         idxType=idxType,
                                         stridable=newStridable,
                                         dom=d, noinit=true);
    alias.data = data;
    //alias.numelm = numelm;
    var i = 1;
    alias.origin = origin;
    for param j in 1..args.size {
      if isRange(args(j)) {
        alias.off(i) = d.dsiDim(i).low;
        alias.origin += blk(j) * (d.dsiDim(i).low - off(j)) / str(j);
        alias.blk(i) = blk(j);
        alias.str(i) = str(j);
        i += 1;
      } else {
        alias.origin += blk(j) * (args(j) - off(j)) / str(j);
      }
    }
    alias.computeFactoredOffs();
    return alias;
  }

  proc dsiReallocate(d: domain) {
    if (d._value.type == dom.type) {
      var copy = new DefaultRectangularArr(eltType=eltType, rank=rank,
                                          idxType=idxType,
                                          stridable=d._value.stridable,
                                          dom=d._value);
      for i in d((...dom.ranges)) do
        copy.dsiAccess(i) = dsiAccess(i);
      off = copy.off;
      blk = copy.blk;
      str = copy.str;
      origin = copy.origin;
      factoredOffs = copy.factoredOffs;
      dsiDestroyData();
      data = copy.data;
      //numelm = copy.numelm;
      delete copy;
    } else {
      halt("illegal reallocation");
    }
  }

  proc dsiLocalSlice(ranges) {
    halt("all dsiLocalSlice calls on DefaultRectangulars should be handled in ChapelArray.chpl");
  }

  proc dsiGetRAD() {
    var rad: _remoteAccessData(eltType, rank, idxType);
    rad.off = off;
    rad.blk = blk;
    rad.str = str;
    rad.origin = origin;
    rad.factoredOffs = factoredOffs;
    rad.data = data;
    return rad;
  }
}

proc DefaultRectangularDom.dsiSerialReadWrite(f /*: Reader or Writer*/) {
  f <~> new ioLiteral("{") <~> ranges(1);
  for i in 2..rank do
    f <~> new ioLiteral(", ") <~> ranges(i);
  f <~> new ioLiteral("}");
}

proc DefaultRectangularDom.dsiSerialWrite(f: Writer) { this.dsiSerialReadWrite(f); }
proc DefaultRectangularDom.dsiSerialRead(f: Reader) { this.dsiSerialReadWrite(f); }

proc DefaultRectangularArr.dsiSerialReadWrite(f /*: Reader or Writer*/) {
  proc recursiveArrayWriter(in idx: rank*idxType, dim=1, in last=false) {
    type strType = chpl__signedType(idxType);
    var makeStridePositive = if dom.ranges(dim).stride > 0 then 1:strType else (-1):strType;
    if dim == rank {
      var first = true;
      if debugDefaultDist && f.writing then f.writeln(dom.ranges(dim));
      for j in dom.ranges(dim) by makeStridePositive {
        if first then first = false;
        else if ! f.binary then f <~> new ioLiteral(" ");
        idx(dim) = j;
        f <~> dsiAccess(idx);
      }
    } else {
      for j in dom.ranges(dim) by makeStridePositive {
        var lastIdx =  dom.ranges(dim).last;
        idx(dim) = j;
        recursiveArrayWriter(idx, dim=dim+1,
                             last=(last || dim == 1) && (j == lastIdx));
      }
    }
    if !last && dim != 1 && ! f.binary then
      f <~> new ioNewline();
  }
  const zeroTup: rank*idxType;
  recursiveArrayWriter(zeroTup);
}

proc DefaultRectangularArr.dsiSerialWrite(f: Writer) { this.dsiSerialReadWrite(f); }
proc DefaultRectangularArr.dsiSerialRead(f: Reader) { this.dsiSerialReadWrite(f); }

// This is very conservative.  For example, it will return false for
// 1-d array aliases that are shifted from the aliased array.
proc DefaultRectangularArr.isDataContiguous() {
  if debugDefaultDistBulkTransfer then
    writeln("isDataContiguous(): origin=", origin, " off=", off, " blk=", blk);
  if origin != 0 then return false;

  for param dim in 1..rank do
    if off(dim)!= dom.dsiDim(dim).first then return false;

  if blk(rank) != 1 then return false;

  for param dim in 1..(rank-1) by -1 do
    if blk(dim) != blk(dim+1)*dom.dsiDim(dim+1).length then return false;

  if debugDefaultDistBulkTransfer then
    writeln("\tYES!");

  return true;
}

proc DefaultRectangularArr.dsiSupportsBulkTransfer() param return true;

proc DefaultRectangularArr.doiCanBulkTransfer() {
  if debugDefaultDistBulkTransfer then writeln("In DefaultRectangularArr.doiCanBulkTransfer()");
  if dom.stridable then
    for param i in 1..rank do
      if dom.ranges(i).stride != 1 then return false;
  if !isDataContiguous(){ 
    if debugDefaultDistBulkTransfer then
      writeln("isDataContiguous return False"); 
    return false;
  }
  return true;
}

proc DefaultRectangularArr.doiCanBulkTransferStride() param {
  if debugDefaultDistBulkTransfer then writeln("In DefaultRectangularArr.doiCanBulkTransferStride()");
  // A DefaultRectangular array is always regular, so bulk should be possible.
  return true;
}

proc DefaultRectangularArr.doiBulkTransfer(B) {
  const Adims = dom.dsiDims();
  var Alo: rank*dom.idxType;
  for param i in 1..rank do
    Alo(i) = Adims(i).first;

  const Bdims = B.domain.dims();
  var Blo: rank*dom.idxType;
  for param i in 1..rank do
    Blo(i) = Bdims(i).first;

  const len = dom.dsiNumIndices:int(32);
  if debugBulkTransfer {
    const elemSize =sizeof(B._value.eltType);
    writeln("In DefaultRectangularArr.doiBulkTransfer():",
            " Alo=", Alo, ", Blo=", Blo,
            ", len=", len, ", elemSize=", elemSize);
  }

  // NOTE: This does not work with --heterogeneous, but heterogeneous
  // compilation does not work right now.  The calls to chpl_comm_get
  // and chpl_comm_put should be changed once that is fixed.
  if this.data.locale.id==here.id {
    if debugDefaultDistBulkTransfer then //See bug in test/optimizations/bulkcomm/alberto/rafatest2.chpl
      writeln("\tlocal get() from ", B._value.locale.id);
    var dest = this.data;
    var src = B._value.data;
    __primitive("chpl_comm_get",
                __primitive("array_get", dest, getDataIndex(Alo)),
                B._value.data.locale.id,
                __primitive("array_get", src, B._value.getDataIndex(Blo)),
                len);
  } else if B._value.data.locale.id==here.id {
    if debugDefaultDistBulkTransfer then
      writeln("\tlocal put() to ", this.locale.id);
    var dest = this.data;
    var src = B._value.data;
    __primitive("chpl_comm_put",
                __primitive("array_get", src, B._value.getDataIndex(Blo)),
                this.data.locale.id,
                __primitive("array_get", dest, getDataIndex(Alo)),
                len);
  } else on this.data.locale {
    if debugDefaultDistBulkTransfer then
      writeln("\tremote get() on ", here.id, " from ", B.locale.id);
    var dest = this.data;
    var src = B._value.data;
    __primitive("chpl_comm_get",
                __primitive("array_get", dest, getDataIndex(Alo)),
                B._value.data.locale.id,
                __primitive("array_get", src, B._value.getDataIndex(Blo)),
                len);
  }
}

/*
Data needed to use strided copy of data:
  + Stridelevels: the number of stride level (not really the number of dimensions because:
     - Stridelevels < rank if we can aggregate several dimensions due to they are consecutive 
         -- for exameple, whole rows --
     - Stridelevels == rank if there is a "by X" whith X>1 in the range description for 
         the rightmost dimension)
  + srcCount: slice size in each dimension for the source array. srcCount[0] should be the number of bytes of contiguous data in the rightmost dimension.
  + dstCount: slice size in each dimension for the destination array. dstCount[0] should be the number of bytes of contiguous data in the rightmost dimension.
  + dstStride: destination array of positive stride distances in bytes to move along each dimension
  + srcStrides: source array of positive stride distances in bytes to move along each dimension
  + dest: destination stating the address of the destination data block
  + src: source stating the address of the source data block
More info in: http://www.escholarship.org/uc/item/5hg5r5fs?display=all
Proposal for Extending the UPC Memory Copy Library Functions and Supporting 
Extensions to GASNet, Version 2.0. Author: Dan Bonachea 

A.doiBulkTransferStride(B) copies B-->A.
*/

proc DefaultRectangularArr.doiBulkTransferStride(Barg,aFromBD=false, bFromBD=false) {
  const A = this, B = Barg._value;

  if debugDefaultDistBulkTransfer then 
    writeln("In DefaultRectangularArr.doiBulkTransferStride ");

  extern proc sizeof(type x): int;
  if debugBulkTransfer then
    writeln("In doiBulkTransferStride: ");
 
  const Adims = A.dom.dsiDims();
  var Alo: rank*dom.idxType;
  for param i in 1..rank do
    Alo(i) = Adims(i).first;
  
  const Bdims = B.dom.dsiDims();
  var Blo: rank*dom.idxType;
  for param i in 1..rank do
    Blo(i) = Bdims(i).first;
  
  if debugDefaultDistBulkTransfer then
    writeln("\tlocal get() from ", B.locale.id);
  
  var dstWholeDim = isWholeDim(A),
      srcWholeDim = isWholeDim(B);
  var stridelevels:int(32);
  
  /* If the stridelevels in source and destination arrays are different, we take the larger*/
  stridelevels=max(A.computeBulkStrideLevels(dstWholeDim),B.computeBulkStrideLevels(srcWholeDim));
  if debugDefaultDistBulkTransfer then 
    writeln("In DefaultRectangularArr.doiBulkTransferStride, stridelevels: ",stridelevels);
  
  //These variables should be actually of size stridelevels+1, but stridelevels is not param...
  
  var srcCount, dstCount:[1..rank+1] int(32);
  
  // Covering the case in which stridelevels has to be incremented after
  // unifying srcCount and dstCount into a single count array. To illustrate the problem:
  
  // config const n = 10 
  // var A: [1..n,1..n] real(64);
  // var B: [1..n,1..n] real(64);
  
  // A[1..10,1..5] = B[1..10, 1..10 by 2]
  
  // Computed variables for chpl_comm_get_strd/puts are:
  //	stridelevels =1
  //	srcCount = (1,50) //In B you read 1 element 50 times
  //	(srcStride=(2) = distance between 1 element and the next one)
  //	dstCount = (5,10) //In A you write a chunk of 5 elments 10
  //	times (dstStride=(10) distance between 1 chunk and the next one)
  
  
  // Since GASNet strided put/get only have a count array, it is
  // necessary to choose the count array that follows the smaller size of
  // the chunks either at the source or destination array. That way the
  // right unified cnt=(1,5,10), which forces to increment stridelevels to
  // 2 and and now srcStride=(2,10) and dstStride=(1,10).
  var dstAux:bool = false;
  var srcAux:bool = false;
  
    if (A.dom.dsiDim(rank).stride>1 && B.dom.dsiDim(rank).stride==1)
    {
      if stridelevels < rank then stridelevels+=1;
      dstAux = true;
    }
    else if (B.dom.dsiDim(rank).stride>1 && A.dom.dsiDim(rank).stride==1)
    {
      if stridelevels < rank then stridelevels+=1;
      srcAux = true;
    }
  /* We now obtain the count arrays for source and destination arrays */
  dstCount= A.computeBulkCount(stridelevels,dstWholeDim,dstAux);
  srcCount= B.computeBulkCount(stridelevels,srcWholeDim,srcAux);
  
/*Then the Stride arrays for source and destination arrays*/
  var dstStride, srcStride: [1..rank] int(32);
  /*When the source and destination arrays have different sizes 
    (example: A[1..10,1..10] and B[1..20,1..20]), the count arrays obtained are different,
    so we have to calculate the minimun count array */
  //Note that we use equal function equal instead of dstCount==srcCount due to the latter fails
  //The same for the array assigment (using assing function instead of srcCount=dstCount)
  
  if !bulkCommEqual(dstCount, srcCount, rank+1) //For different size arrays
  {
      for h in 1..stridelevels+1
      {
	if dstCount[h]>srcCount[h]
        {
	  bulkCommAssign(dstCount,srcCount, stridelevels+1);
          break;
        }
        else if dstCount[h]<srcCount[h] then break; 
      }
  }
    
  dstStride = computeBulkStride(dstWholeDim,dstCount,stridelevels,aFromBD);
  srcStride = B.computeBulkStride(srcWholeDim,dstCount,stridelevels,bFromBD);
  
  doiBulkTransferStrideComm(Barg, stridelevels, dstStride, srcStride, dstCount, srcCount, Alo, Blo);
}

//
// Invoke the primitives chpl_comm_get_strd/puts, depending on what locale
// we are on vs. where the source and destination are.
// The logic mimics that in doiBulkTransfer().
//
proc DefaultRectangularArr.doiBulkTransferStrideComm(B, stridelevels, dstStride, srcStride, dstCount, srcCount, Alo, Blo)
 { 
 //writeln("Locale: ", here.id, " stridelvl: ", stridelevels, " DstStride: ", dstStride," SrcStride: ",srcStride, " Count: ", dstCount, " dst.Blk: ",blk, " src.Blk: ",B._value.blk/*, " dom: ",dom.dsiDims()," B.blk: ",B._value.blk," B.dom: ",B._value.dom.dsiDims()*/);
  //CASE 1: when the data in destination array is stored "here", it will use "chpl_comm_get_strd". 
  if this.data.locale==here
  {
    var dest = this.data;
    var src = B._value.data;
    
    var dststr=dstStride._value.data;
    var srcstr=srcStride._value.data;
    var cnt=dstCount._value.data;

    if debugBulkTransfer {
      writeln("Case 1");
      writeln("Locale:",here.id,"stridelevel: ", stridelevels);
      writeln("Locale:",here.id,"Count: ",dstCount);
      writeln("Locale:",here.id," dststrides: ",dstStride);
      writeln("Locale:",here.id,",srcstrides: ",srcStride);
    }
    var srclocale =B._value.data.locale.id : int(32);
       __primitive("chpl_comm_get_strd",
                    __primitive("array_get",dest, getDataIndex(Alo)),
                    __primitive("array_get",dststr,dstStride._value.getDataIndex(1)), 
		    srclocale,
                    __primitive("array_get",src, B._value.getDataIndex(Blo)),
                    __primitive("array_get",srcstr,srcStride._value.getDataIndex(1)),
                    __primitive("array_get",cnt, dstCount._value.getDataIndex(1)),
                    stridelevels);
  }
  //CASE 2: when the data in source array is stored "here", it will use "chpl_comm_put_strd". 
  else if B._value.data.locale==here
  {
    if debugDefaultDistBulkTransfer then
      writeln("\tlocal put() to ", this.locale.id);
    
    var dststr=dstStride._value.data;
    var srcstr=srcStride._value.data;
    var cnt=srcCount._value.data;
    
    if debugBulkTransfer {
      writeln("Case 2");
      writeln("stridelevel: ",stridelevels);
      writeln("Count: ",srcCount);
      writeln("dststrides: ",dstStride);
      writeln("srcstrides: ",srcStride);
      writeln("Blk: ",blk);
    }
    
    var dest = this.data;
    var src = B._value.data;
    var destlocale =this.data.locale.id : int(32);

    __primitive("chpl_comm_put_strd",
      		  __primitive("array_get",dest,getDataIndex(Alo)),
      		  __primitive("array_get",dststr,dstStride._value.getDataIndex(1)),
                  destlocale,
                  __primitive("array_get",src,B._value.getDataIndex(Blo)),
      		  __primitive("array_get",srcstr,srcStride._value.getDataIndex(1)),
      		  __primitive("array_get",cnt, srcCount._value.getDataIndex(1)),
      		  stridelevels);
  }
  //CASE 3: other case, it will use "chpl_comm_get_strd". 
  else on this.data.locale
  {   
    var dest = this.data;
    var src = B._value.data;

    //We are in a locale that doesn't store neither A nor B so we need to copy the auxiliarry
    //arrays to the locale that hosts A. This should translate into some more gets...
    var count:[1..(stridelevels+1)] int(32);
    count=dstCount;
  
    var dststrides,srcstrides:[1..stridelevels] int(32);
    srcstrides=srcStride;
    dststrides=dstStride;
    
    var dststr=dststrides._value.data;
    var srcstr=srcstrides._value.data;
    var cnt=count._value.data;
    
    if debugBulkTransfer {
      writeln("Case 3");
      writeln("stridelevel: ", stridelevels);
      writeln("Count: ",count);
      writeln("dststrides: ",dststrides);
      writeln("srcstrides: ",srcstrides);
    }
    
    var srclocale =B._value.data.locale.id : int(32);
       __primitive("chpl_comm_get_strd",
                    __primitive("array_get",dest, getDataIndex(Alo)),
                    __primitive("array_get",dststr,dststrides._value.getDataIndex(1)), 
                    srclocale,
                    __primitive("array_get",src, B._value.getDataIndex(Blo)),
                    __primitive("array_get",srcstr,dststrides._value.getDataIndex(1)),
                    __primitive("array_get",cnt, count._value.getDataIndex(1)),
                    stridelevels);   
  }
}

proc DefaultRectangularArr.isDefaultRectangular() param{return true;}

/* This function returns stridelevels for the default rectangular array.
  + Stridelevels: the number of stride level (not really the number of dimensions because:
     - Stridelevels < rank if we can aggregate several dimensions due to they are consecutive 
         -- for exameple, whole rows --
     - Stridelevels == rank if there is a "by X" whith X>1 in the range description for 
         the rightmost dimension)*/
proc DefaultRectangularArr.computeBulkStrideLevels(rankcomp):int(32) where rank == 1
{//To understand the blk(1)==1 condition,
  //see test/optimizations/bulkcomm/alberto/test_rank_change2.chpl(example 4)
  if dom.dsiStride==1 && blk(1)==1 then return 0;
  else return 1;
}

//Cases for computeBulkStrideLevels where rank>1:
//Case 1:  
//  var A: [1..4,1..4,1..4] real; A[1..4,3..4,1..4 by 2] 
// --> In dimension 3 there is stride, because the elements are not
//     consecutives, so stridelevels +=1
//More in test/optimizations/bulkcomm/alberto/2dDRtoBDTest.chpl (example 8)

//Case 2:
//   Locales = 4
//   var Dist1 = new dmap(new Block({1..4,1..4,1..4}));
//   var Dom1: domain(3,int) dmapped Dist1 ={1..4,1..4,1..4};
//   var A:[Dom1] int;
//   A[1..4,2..4..3,1..1]--> blk:(8,4,1) 
//   A[1..4,2..4,1]--> A[1..4,2..4] --> blk:(8,4) --> we need to check the blk array. 
//More in test/optimizations/bulkcomm/alberto/rankchange.chpl (example 5)

//Example for Case 3:  
//  var A: [1..4,1..4,1..4] real; A[1..4,3..4,1..4 by 3] 
//   --> the distance between the elements [1,3,4] and [1,4,1] is 1 element,
//   while the distance between the elements in the rightmost dimension (rank) 
//   is 3 positions([1,3,1],[1,3,4]), so the checkStrideDistance(i) for i=2 
//   will return false, therefore, stridelevels +=1
//More in test/optimizations/bulkcomm/alberto/2dDRtoBDTest.chpl (example 4)
proc DefaultRectangularArr.computeBulkStrideLevels(rankcomp):int(32) where rank > 1 
{
  var stridelevels:int(32) = 0;
  if (dom.dsiStride(rank)>1 && dom.dsiDim(rank).length>1) //CASE 1 
  || (blk(rank)>1 && dom.dsiDim(rank).length>1) //CASE 2   
  then stridelevels+=1; //In many tests, both cases are true
 
  for param i in 2..rank by -1 do
    if (dom.dsiDim(i-1).length>1 && !checkStrideDistance(i)) //CASE 3
      then stridelevels+=1; 
  
  return stridelevels;
}

/* This function returns the count array for the default rectangular array. */
proc DefaultRectangularArr.computeBulkCount(stridelevels:int(32), rankcomp, aux = false):(rank+1)*int(32) where rank ==1
{
  var c: (rank+1)*int(32);
  //To understand the blk(1)>1 condition,
  //see test/optimizations/bulkcomm/alberto/test_rank_change2.chpl(example 4)
  if dom.dsiStride > 1 || blk(1)>1 {
    c[1]=1;
    c[2]=dom.dsiDim(1).length:int(32);
  }
  else
    c[1]=dom.dsiDim(1).length:int(32);
  return c;
}

//Cases for computeBulkCount where rank>1:
//Case 1:  
//  var A: [1..4,1..4,1..4] real; A[1..4,3..4,1..4 by 2] 
//    --> In dimension 3 there is stride, because the elements 
//        are not consecutive, so c[1] = 1
//More in test/optimizations/bulkcomm/alberto/perfTest.chpl (DR <- DR example 8)

//Case 2:
//    Locales = 4
//    var Dist1 = new dmap(new Block({1..4,1..4,1..4}));
//    var Dom1: domain(3,int) dmapped Dist1 ={1..4,1..4,1..4};
//    var A:[Dom1] int;
//    A[1..4,2..4,1..1]--> blk:(8,4,1) 
//    A[1..4,2..4,1]--> A[1..4,2..4] --> blk:(8,4) --> c[1] = 1;
//More in test/optimizations/bulkcomm/alberto/rankchange.chpl (example 5)

//Case 3:  
//    var A: [1..4,1..4,1..4] real; A[1..4,3..4,1..4 by 2] 
//      --> the distance between the elements [1,3,3] and [1,4,1] is 2 elements,
//          and the distance between the elements in the rightmost dimension(rank) 
//          is also 2 positions([1,3,1],[1,3,3]), so it is possible to join both 
//          dimensions, and the new count value is count[2] = 4 (2 x 2).
//
//Case 4:  
//    var A: [1..4,1..4,1..4] real; A[1..4,4..4,1..4 by 3] 
//      --> There is only 1 element in dimension 2, so it's possible to 
//        join to dimension 1, and the value of count[3] will be the number 
//        of elements in dimension 2 x number of elements in dimesion 1 (1 x 4).
//More in test/optimizations/bulkcomms/alberto/3dAgTestStride.chpl (example 6 and 7)
proc DefaultRectangularArr.computeBulkCount(stridelevels:int(32), rankcomp, aux = false):(rank+1)*int(32) where rank >1
{
  var c: (rank+1)*int(32) ;
  var init:int(32)=1;
//var dim is used to point to the analyzed dimension at each iteration
//due to the same stride can be valid across two contiguous dimensions
  var dim:int =rank;
  var tmp:int(32)=1;
  if (dom.dsiStride(rank)>1 && dom.dsiDim(rank).length>1) //CASE 1
    ||(blk(rank)>1 && dom.dsiDim(rank).length>1) //CASE 2
    {c[1]=1; init=2;}
//If the first value of count, c[1] have been already computed then
//compute the rest of count starting at dim 2 (init=2)  
  for i in init..stridelevels+1 do
  {
    if dim == 0 then c[i]=1;//the leftmost dimension 
    else
      {
        c[i]=this.dom.dsiDim(dim).length:int(32);
//find the next dimension for which the next different stride arises
	for h in 2..dim by -1:int(32) 
        {
//The aux variable is to cover the case in which stridelevels has to be
// incremented after unifying srcCount and dstCount into a single count array,
// and the condition h!=rank is because the new count value it's always in the 
// rightmost dimension.
// See lines 850-871
	  if( (checkStrideDistance(h) && (!aux || h!=rank))//CASE 3
             || (dom.dsiDim(h).length==1&& h!=rank)) //CASE 4
	    {
	      c[i]*=dom.dsiDim(h-1).length:int(32);
              dim -= 1;
	    }
	  else break;  
        }
        dim -= 1;
      }
  }
  return c;
}

/* This function returns the stride array for the default rectangular array. */
//Case 1:  
//  var A: [1..4,1..4,1..4] real; A[1..4,3..4,1..4 by 2] 
//    --> In the rightmost dimension(3) there is stride, so Stride[1] = 2
//More in test/optimizations/bulkcomm/alberto/perfTest.chpl (DR <- DR example 8)

//Case 2:
//    Locales = 4
//    var Dist1 = new dmap(new Block({1..4,1..4,1..4}));
//    var Dom1: domain(3,int) dmapped Dist1 ={1..4,1..4,1..4};
//    var A:[Dom1] int;
//    A[1..4,2..4,1..1]--> blk:(8,4,1) 
//    A[1..4,2..4,1]--> A[1..4,2..4] --> blk:(8,4) --> Stride[1] = blk(2) = 4;
//More in test/optimizations/bulkcomm/alberto/rankchange.chpl (example 5)

//Case 3:
//    Locales = 2
//    var Dist1 = new dmap(new Block({1..4,1..4,1..4}));
//    var Dom1: domain(3,int) dmapped Dist1 ={1..4,1..4,1..4};
//    var A:[Dom1] int;
//    A[1..4 by 2,2..4 by 2,1..4 by 3]--> blk:(32,8,3)
//    --> To get the value in Stride[2] we only need to check if the actual
//        dimension has enough number of elements.
//        To do this, we use a cumulative variable until the number of elements 
//        are equal to count[3]. Then Stride[2] = blk(2) = 8, because this DR
//        is part of a BD array.
//More in test/optimizations/bulkcomm/alberto/perfTest_v2.chpl (BD <- BD Example 13)

//Case 4:  
//    var A: [1..4,1..4,1..4] real; A[1..4 by 2,2..4 by 2,1..4 by 3] --> blk(16,4,1)
//      --> To get the value in Stride[2] we only need to check if the actual
//        dimension has enough number of elements.
//        To do this, we use a cumulative variable until the number of elements
//        are equal to count[3]. Then Stride[2] = blk(1) * Dim(1).stride = 16 * 2 = 32.
//        In this case it's necessary to use the "Dim(1).stride" because the DR is not
//        part of a BD Array, so the aFromBD variable is set to false, and the
//        value of blk array is different than when that variable is set to true,
//        as you can observe in the previous example (case 3)
//More in test/optimizations/bulkcomms/alberto/3dAgTestStride.chpl (example 6 and 7)

proc DefaultRectangularArr.computeBulkStride(rankcomp,cnt:[],levels:int(32), aFromBD=false)
{
  var c: rank*int(32); 
  var h=1; //Stride array index
  var cum=1; //cumulative variable
  
  if (cnt[h]==1 && dom.dsiDim(rank).length>1)
  {//To understand the blk[rank]==1 condition,
  //see test/optimizations/bulkcomm/alberto/test_rank_change2.chpl(example 12)
    if !aFromBD && blk[rank]==1 then c[h]=dom.dsiDim(rank).stride:int(32); //CASE 1
    else c[h]=blk[rank]:int(32); //CASE 2
    h+=1;
  }
 
  for param i in 2..rank by -1:int(32){
    if (levels>=h)
    {
      if (cnt[h]==dom.dsiDim(i).length*cum && dom.dsiDim(i-1).length>1) //CASE 3
      {//now, we are in the right dimension (i dimension) to obtain the stride value
        c[h]=blk(i-1):int(32);
        if !aFromBD then //CASE 4
          c[h]*= dom.dsiDim(i-1).stride:int(32); 
        h+=1; //Increment the index
        cum=1; //reset the cumulative variable
      }
      else cum=cum*dom.dsiDim(i).length;
    }
  }
  return c;
}

/*
This function is used to help in the aggregation of data from different array dimensions.
The function returns an array in which each position is associated to one of the dimensions
of the domain. Each array component can be true or false, pointing out whether or not the rank
for that particular dimension covers the whole dimension. 
For instance, if for one array A, the domain is D=[1..4, 2..6, 1..6] and we refer to 
A[1..3, 3..5,1..6] the resulting array will be [false, false, true].
Note that the leftmost dimension is really not necessary due to the first dimension can not be 
aggregated with any other one (there is no more dimensions beyond the first one). Therefore, a 
subtle possible optimization would be to declare the resulting array 2..rank instead of 1..rank
*/ 
proc DefaultRectangularArr.isWholeDim(d) where rank==1
{
  return true;
}

proc DefaultRectangularArr.isWholeDim(d) where rank>1
{
 var c:d.rank*bool;
  for param i in 2..rank do
    if (d.dom.dsiDim(i).length==d.blk(i-1)/d.blk(i) && dom.dsiStride(i)==1) then c[i]=true;

  return c;
}

/* This function checks if the stride in dimension 'x' is the same as the distance
between the last element in dimension 'x' and  the first in dimension 'x-1'.
If the distances are equal, we can aggregate these two dimmensions. 
Example: 
array A, the domain is D=[1..6, 1..6, 1..6]
Let's A[1..6 by 2, 1..6, 1..6 by 2], then, checkStrideDistance(3) returns true, 
due to when jumping from row to row (from the last element of one row, to the 
first element of the next one) the stride is the same than when jumping from 
elements inside the row. 
*/
proc DefaultRectangularArr.checkStrideDistance(x: int)
{
  if dom.dsiDim(x-1).length==1 then return false;

  if (blk(x-1)*dom.dsiStride(x))/blk(x) - (1+dom.dsiDim(x).last - dom.dsiDim(x).first) == dom.dsiStride(x)-1
  && dom.dsiDim(x).length>1
  && (dom.dsiStride(x-1)==1 || dom.dsiDim(x-1).length==1)
  then return true;

  return false;
}

//
// Check whether the first 'tam' elements in arrays 'd1' and 'd2' are equal.
// This is better than 'd1==d2' because it does not result in a forall.
// TODO: convert to 'for' for rank 1..5. (The caller must ensure that
// d1 and d2 are always equal at indices tam+1..rank.)
// Ideally, d1 and d2 will become tuples.
//
proc bulkCommEqual(d1:[], d2:[], tam:int)
{
  var c:bool = true;
  for i in 1..tam do if d1[i]!=d2[i] then c=false;
  return c;
}

//
// Assign the first 'tam' elements from array 'd2' to array 'd1'.
// This is better than 'd1=d2' because it does not result in a forall.
// TODO: convert to 'for' for rank 1..5.
// Ideally, d1 and d2 will become tuples.
//
proc bulkCommAssign(d1:[], d2:[], tam: int)
{
  for i in 1..tam do d1[i]=d2[i];
}

// Work around the tuple(1) vs. scalar issue.
proc DefaultRectangularArr.tuplify(arg) {
  if isTuple(arg) then return arg; else return tuple(arg);
}

//
// bulkConvertCoordinate() converts
//   point 'b' within 'Barr.domain'
// to
//   point within 'Aarr.domain'
// that has the same indexOrder in each dimension.
//
// This function was contributed by Juan Lopez and later improved by Alberto.
// In the SBAC'12 paper it is called m().
//
proc bulkCommConvertCoordinate(bArg, Barr, Aarr)
{
  compilerAssert(Aarr.rank == Barr.rank);
  const b = chpl__tuplify(bArg);
  param rank = Aarr.rank;
  const AD = Aarr.dom.dsiDims();
  const BD = Barr.dom.dsiDims();
  var result: rank * int;
  for param i in 1..rank {
    const ar = AD(i), br = BD(i);
    //writeln("ar: ",ar," br: ",br," b: ",b," b(i): ",b(i)," br.indexOrder(b(i)): ",br.indexOrder(b(i)));
    if boundsChecking then assert(br.member(b(i)));
    result(i) = ar.orderToIndex(br.indexOrder(b(i)));
  }
  return result;
}

}
