// Copyright (c) 2004-2012, Cray Inc.  (See LICENSE file for more details)


// DefaultAssociative.chpl
//
pragma "no use ChapelStandard"
module DefaultAssociative {

use DSIUtil;
config param debugDefaultAssoc = false;
config param debugAssocDataPar = false;

use Sort /* only QuickSort */;

// TODO: make the domain parameterized by this?
type chpl_table_index_type = int;


/* These declarations could/should both be nested within
   DefaultAssociativeDom? */
enum chpl__hash_status { empty, full, deleted };
_ensure_reference_type(chpl__hash_status);

record chpl_TableEntry {
  type idxType;
  var status: chpl__hash_status = chpl__hash_status.empty;
  var idx: idxType;
}

proc chpl__primes return (23, 53, 97, 193, 389, 769, 1543,
                         3079, 6151, 12289, 24593, 49157, 98317, 196613,
                         393241, 786433, 1572869, 3145739, 6291469, 12582917, 25165843,
                         50331653, 100663319, 201326611, 402653189, 805306457, 1610612741);

class DefaultAssociativeDom: BaseAssociativeDom {
  type idxType;
  param parSafe: bool;

  var dist: DefaultDist;

  // The guts of the associative domain

  // We explicitly use processor atomics here since this is not
  // by design a distributed data structure
  var numEntries: atomic_int64;
  var tableLock: atomicflag; // do not access directly, use function below
  var tableSizeNum = 1;
  var tableSize = chpl__primes(tableSizeNum);
  var tableDom = {0..tableSize-1};
  var table: [tableDom] chpl_TableEntry(idxType);

  inline proc lockTable() {
    while tableLock.testAndSet() do chpl_task_yield();
  }

  inline proc unlockTable() {
    tableLock.clear();
  }

  // TODO: An ugly [0..-1] domain appears several times in the code --
  //       replace with a named constant/param?
  var postponeResize = false;

  proc linksDistribution() param return false;
  proc dsiLinksDistribution()     return false;

  proc DefaultAssociativeDom(type idxType,
                             param parSafe: bool,
                             dist: DefaultDist) {
    this.dist = dist;
  }

  //
  // Standard Internal Domain Interface
  //
  proc dsiBuildArray(type eltType) {
    return new DefaultAssociativeArr(eltType=eltType, idxType=idxType,
                                     parSafeDom=parSafe, dom=this);
  }

  proc dsiSerialReadWrite(f /*: Reader or Writer*/) {
    var first = true;
    f <~> new ioLiteral("{");
    for idx in this {
      if first then 
        first = false; 
      else 
        f <~> new ioLiteral(", ");
      f <~> idx;
    }
    f <~> new ioLiteral("}");
  }
  proc dsiSerialWrite(f: Writer) { this.dsiSerialReadWrite(f); }
  proc dsiSerialRead(f: Reader) { this.dsiSerialReadWrite(f); }

  //
  // Standard user domain interface
  //

  inline proc dsiNumIndices {
    return numEntries.read();
  }

  iter dsiIndsIterSafeForRemoving() {
    postponeResize = true;
    for i in this.these() do
      yield i;
    on this {
      postponeResize = false;
      if (numEntries.read()*8 < tableSize && tableSizeNum > 1) {
        if parSafe then lockTable();
        if (numEntries.read()*8 < tableSize && tableSizeNum > 1) {
          _resize(grow=false);
        }
        if parSafe then unlockTable();
      }
    }
  }

  iter these() {
    if !_isEnumeratedType(idxType) {
      for slot in _fullSlots() {
        yield table(slot).idx;
      }
    } else {
      for val in chpl_enumerate(idxType) {
        var (match, slot) = _findFilledSlot(val);
        if match then
          yield table(slot).idx;
      }
    }
  }

  iter these(param tag: iterKind) where tag == iterKind.leader {
    if debugDefaultAssoc then
      writeln("*** In domain leader code:");
    const numTasks = if dataParTasksPerLocale==0 then here.numCores
                     else dataParTasksPerLocale;
    const ignoreRunning = dataParIgnoreRunningTasks;
    const minIndicesPerTask = dataParMinGranularity;
    // We are simply slicing up the table here.  Trying to do something
    //  more intelligent (like evenly dividing up the full slots, led
    //  to poor speed ups.
    // This requires that the zipppered domains match.
    const numIndices = tableSize;

    if debugAssocDataPar {
      writeln("### numTasks = ", numTasks);
      writeln("### ignoreRunning = ", ignoreRunning);
      writeln("### minIndicesPerTask = ", minIndicesPerTask);
    }

    if debugDefaultAssoc then
      writeln("    numTasks=", numTasks, " (", ignoreRunning,
              "), minIndicesPerTask=", minIndicesPerTask);

    var numChunks = _computeNumChunks(numTasks, ignoreRunning,
                                      minIndicesPerTask,
                                      numIndices);
    if debugDefaultAssoc then
      writeln("    numChunks=", numChunks, "length=", numIndices);

    if debugAssocDataPar then writeln("### numChunks=", numChunks);

    if numChunks == 1 {
      yield (0..numIndices-1, this);
    } else {
      coforall chunk in 0..#numChunks {
        const (lo, hi) = _computeBlock(numIndices, numChunks,
                                       chunk, numIndices-1);
        if debugDefaultAssoc then
          writeln("*** DI[", chunk, "]: tuple = ", tuple(lo..hi));
        yield (lo..hi, this);
      }
    }
  }

  iter these(param tag: iterKind, followThis) where tag == iterKind.follower {
    var (chunk, followThisDom) = followThis;
    if followThisDom != this {
      // check to see if domains match
      var followThisTab = followThisDom.table;
      var myTab = table;
      var mismatch = false;
      // could use a reduction
      for slot in chunk.low..chunk.high do
        if followThisTab(slot).status != myTab(slot).status {
          mismatch = true;
          break;
        }
      if mismatch then
        halt("zippered associative domains do not match");
    }

    if debugDefaultAssoc then
      writeln("In domain follower code: Following ", chunk);

    for slot in chunk.low..chunk.high do
      if table(slot).status == chpl__hash_status.full then
        yield table(slot).idx;
  }

  //
  // Associative Domain Interface
  //
  proc dsiClear() {
    on this {
      if parSafe then lockTable();
      for slot in tableDom {
        table(slot).status = chpl__hash_status.empty;
      }
      numEntries.write(0);
      if parSafe then unlockTable();
    }
  }

  proc dsiMember(idx: idxType): bool {
    return _findFilledSlot(idx)(1);
  }

  proc dsiAdd(idx: idxType): index(tableDom) {
    var slotNum: index(tableDom);
    on this {
      if parSafe then lockTable();
      if ((numEntries.read()+1)*2 > tableSize) {
        _resize(grow=true);
      }
      slotNum = _add(idx);
      if parSafe then unlockTable();
    }
    return slotNum;
  }

  // This routine adds new indices without checking the table size and
  //  is thus appropriate for use by routines like _resize().
  proc _add(idx: idxType): index(tableDom) {
    const (foundSlot, slotNum) = _findEmptySlot(idx);
    if (foundSlot) {
      table(slotNum).status = chpl__hash_status.full;
      table(slotNum).idx = idx;
      numEntries.add(1);
    } else {
      if (slotNum < 0) {
        halt("couldn't add ", idx, " -- ", numEntries.read(), " / ", tableSize, " taken");
        return -1;
      }
      // otherwise, re-adding an index that's already in there
    }
    return slotNum;
  }

  proc dsiRemove(idx: idxType) {
    on this {
      if parSafe then lockTable();
      const (foundSlot, slotNum) = _findFilledSlot(idx);
      if (foundSlot) {
        for a in _arrs do
          a.clearEntry(idx);
        table(slotNum).status = chpl__hash_status.deleted;
        numEntries.sub(1);
      } else {
        halt("index not in domain: ", idx);
      }
      if (numEntries.read()*8 < tableSize && tableSizeNum > 1) {
        _resize(grow=false);
      }
      if parSafe then unlockTable();
    }
  }

  iter dsiSorted() {
    var tableCopy: [0..#numEntries.read()] idxType;

    for (tmp, slot) in zip(tableCopy.domain, _fullSlots()) do
      tableCopy(tmp) = table(slot).idx;

    QuickSort(tableCopy);

    for ind in tableCopy do
      yield ind;
  }

  //
  // Internal interface (private)
  //
  // NOTE: Calls to this routine assume that the tableLock has been acquired.
  //
  proc _resize(grow:bool) {
    if postponeResize then return;
    // back up the arrays
    _backupArrays();

    // copy the table (TODO: could use swap between two versions)
    var copyDom = tableDom;
    var copyTable: [copyDom] chpl_TableEntry(idxType) = table;

    // grow original table
    tableDom = {0..(-1:chpl_table_index_type)}; // non-preserving resize
    numEntries.write(0); // reset, because the adds below will re-set this
    tableSizeNum += if grow then 1 else -1;
    if tableSizeNum > chpl__primes.size then halt("associative array exceeds maximum size");
    tableSize = chpl__primes(tableSizeNum);
    tableDom = {0..tableSize-1};

    // insert old data into newly resized table
    for slot in _fullSlots(copyTable) {
      const newslot = _add(copyTable(slot).idx);
      _preserveArrayElements(oldslot=slot, newslot=newslot);
    }
    
    _removeArrayBackups();
  }

  proc _findFilledSlot(idx: idxType, tab = table): (bool, index(tableDom)) {
    for slotNum in _lookForSlots(idx, tab.domain.high+1) {
      const slotStatus = tab(slotNum).status;
      if (slotStatus == chpl__hash_status.empty) {
        return (false, -1);
      } else if (slotStatus == chpl__hash_status.full) {
        if (tab(slotNum).idx == idx) {
          return (true, slotNum);
        }
      }
    }
    return (false, -1);
  }

  proc _findEmptySlot(idx: idxType): (bool, index(tableDom)) {
    for slotNum in _lookForSlots(idx) {
      const slotStatus = table(slotNum).status;
      if (slotStatus == chpl__hash_status.empty ||
          slotStatus == chpl__hash_status.deleted) {
        return (true, slotNum);
      } else if (table(slotNum).idx == idx) {
        return (false, slotNum);
      }
    }
    return (false, -1);
  }
    
  iter _lookForSlots(idx: idxType, numSlots = tableSize) {
    const baseSlot = chpl__defaultHashWrapper(idx);
    for probe in 0..numSlots/2 {
      yield (baseSlot + probe**2)%numSlots;
    }
  }

  iter _fullSlots(tab = table) {
    for slot in tab.domain {
      if tab(slot).status == chpl__hash_status.full then
        yield slot;
    }
  }
}


class DefaultAssociativeArr: BaseArr {
  type eltType;
  type idxType;
  param parSafeDom: bool;
  var dom : DefaultAssociativeDom(idxType, parSafe=parSafeDom);

  var data : [dom.tableDom] eltType;

  var tmpDom = {0..(-1:chpl_table_index_type)};
  var tmpTable: [tmpDom] eltType;

  //
  // Standard internal array interface
  // 

  proc dsiGetBaseDom() return dom;

  proc clearEntry(idx: idxType) {
    const initval: eltType;
    dsiAccess(idx) = initval;
  }

  proc dsiAccess(idx : idxType) var : eltType {
    const (found, slotNum) = dom._findFilledSlot(idx);
    if (found) then
      return data(slotNum);
    else {
      halt("array index out of bounds: ", idx);
      return data(0);
    }
  }

  iter these() var {
    for slot in dom {
      yield dsiAccess(slot);
    }
  }

  iter these(param tag: iterKind) where tag == iterKind.leader {
    for followThis in dom.these(tag) do
      yield followThis;
  }

  iter these(param tag: iterKind, followThis) var where tag == iterKind.follower {
    var (chunk, followThisDom) = followThis;
    if followThisDom != dom {
      // check to see if domains match
      var followThisTab = followThisDom.table;
      var myTab = dom.table;
      var mismatch = false;
      // could use a reduction
      for slot in chunk.low..chunk.high do
        if followThisTab(slot).status != myTab(slot).status {
          mismatch = true;
          break;
        }
      if mismatch then
        halt("zippered associative array does not match the iterated domain");
    }
    if debugDefaultAssoc then
      writeln("In array follower code: Following ", chunk);
    var tab = dom.table;  // cache table for performance
    for slot in chunk.low..chunk.high do
      if tab(slot).status == chpl__hash_status.full then
        yield data(slot);
  }

  proc dsiSerialReadWrite(f /*: Reader or Writer*/) {
    var first = true;
    for val in this {
      if (first) then
        first = false;
      else
        f <~> new ioLiteral(" ");
      f <~> val;
    }
  }
  proc dsiSerialWrite(f: Writer) { this.dsiSerialReadWrite(f); }
  proc dsiSerialRead(f: Reader) { this.dsiSerialReadWrite(f); }


  //
  // Associative array interface
  //

  iter dsiSorted() {
    var tableCopy: [0..dom.dsiNumIndices-1] eltType;
    for (copy, slot) in zip(tableCopy.domain, dom._fullSlots()) do
      tableCopy(copy) = data(slot);

    QuickSort(tableCopy);

    for elem in tableCopy do
      yield elem;
  }


  //
  // Internal associative array interface
  //

  proc _backupArray() {
    tmpDom = dom.tableDom;
    tmpTable = data;
  }

  proc _removeArrayBackup() {
    tmpDom = {0..(-1:chpl_table_index_type)};
  }

  proc _preserveArrayElement(oldslot, newslot) {
    data(newslot) = tmpTable(oldslot);
  }
}


proc chpl__defaultHashWrapper(x): chpl_table_index_type {
  const hash = chpl__defaultHash(x); 
  return (hash & max(chpl_table_index_type)): chpl_table_index_type;
}


// Thomas Wang's 64b mix function from http://www.concentric.net/~Ttwang/tech/inthash.htm
proc _gen_key(i: int(64)): int(64) {
  var key = i;
  key += ~(key << 32);
  key = key ^ (key >> 22);
  key += ~(key << 13);
  key = key ^ (key >> 8);
  key += (key << 3);
  key = key ^ (key >> 15);
  key += ~(key << 27);
  key = key ^ (key >> 31);
  return (key & max(int(64))): int(64);  // YAH, make non-negative
}

inline proc chpl__defaultHash(b: bool): int(64) {
  if (b) then
    return 0;
  else
    return 1;
}

inline proc chpl__defaultHash(i: int(64)): int(64) {
  return _gen_key(i);
}

inline proc chpl__defaultHash(u: uint(64)): int(64) {
  return _gen_key(u:int(64));
}

inline proc chpl__defaultHash(f: real): int(64) {
  return _gen_key(__primitive( "real2int", f));
}

inline proc chpl__defaultHash(c: complex): int(64) {
  return _gen_key(__primitive("real2int", c.re) ^ __primitive("real2int", c.im)); 
}

inline proc chpl__defaultHash(u: chpl_taskID_t): int(64) {
  return _gen_key(u:int(64));
}

// Use djb2 (Dan Bernstein in comp.lang.c.
inline proc chpl__defaultHash(x : string): int(64) {
  var hash: int(64) = 0;
  for c in 1..(x.length) {
    hash = ((hash << 5) + hash) ^ ascii(x.substring(c));
  }
  return _gen_key(hash);
}

inline proc chpl__defaultHash(o: object): int(64) {
  return _gen_key(__primitive( "object2int", o));
}

}
