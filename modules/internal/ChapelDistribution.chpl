// Copyright (c) 2004-2012, Cray Inc.  (See LICENSE file for more details)


pragma "no use ChapelStandard"
module ChapelDistribution {

use List;

config const dataParTasksPerLocale = 0;
config const dataParIgnoreRunningTasks = true;
config const dataParMinGranularity: int = 1;

extern proc chpl_task_yield();

// NOTE: If module initialization order changes, this may be affected
if dataParTasksPerLocale<0 then halt("dataParTasksPerLocale must be >= 0");
if dataParMinGranularity<=0 then halt("dataParMinGranularity must be > 0");

//
// Abstract distribution class
//
pragma "base dist"
class BaseDist {
  var _distCnt: atomic int;   // distribution reference count
  var _doms: list(BaseDom);   // domains declared over this domain
  var _domsLock: atomic bool; //   and lock for concurrent access

  pragma "dont disable remote value forwarding"
  proc destroyDist(dom: BaseDom = nil) {
    if dom then remove_dom(dom);
    var cnt = _distCnt.fetchSub(1)-1;
    if !noRefCount {
      if cnt < 0 then
        halt("distribution reference count is negative!");
    } else {
      if cnt > 0 then
        halt("distribution reference count has been modified!");
    }
    return cnt;
  }

  inline proc remove_dom(x) {
    on this {
      _lock_doms();
      _doms.remove(x);
      _unlock_doms();
    }
  }

  inline proc add_dom(x) {
    on this {
      _lock_doms();
      _doms.append(x);
      _unlock_doms();
    }
  }

  inline proc _lock_doms() {
    // WARNING: If you are calling this function directly from
    // a remote locale, you should consider wrapping the call in
    // an on clause to avoid excessive remote forks due to the
    // testAndSet()
    while (_domsLock.testAndSet()) do chpl_task_yield();
  }

  inline proc _unlock_doms() {
    _domsLock.clear();
  }

  proc dsiNewRectangularDom(param rank: int, type idxType, param stridable: bool) {
    compilerError("rectangular domains not supported by this distribution");
  }

  proc dsiNewAssociativeDom(type idxType, param parSafe: bool) {
    compilerError("associative domains not supported by this distribution");
  }

  proc dsiNewAssociativeDom(type idxType, param parSafe: bool)
  where _isEnumeratedType(idxType) {
    compilerError("enumerated domains not supported by this distribution");
  }

  proc dsiNewOpaqueDom(type idxType, param parSafe: bool) {
    compilerError("opaque domains not supported by this distribution");
  }

  proc dsiNewSparseDom(param rank: int, type idxType, dom: domain) {
    compilerError("sparse domains not supported by this distribution");
  }

  proc dsiSupportsPrivatization() param return false;
  proc dsiRequiresPrivatization() param return false;

  proc dsiDestroyDistClass() { }

  proc dsiDisplayRepresentation() { }
}

//
// Abstract domain classes
//
pragma "base domain"
class BaseDom {
  var _domCnt: atomic int;    // domain reference count
  var _arrs: list(BaseArr);   // arrays declared over this domain
  var _arrsLock: atomic bool; //   and lock for concurrent access

  proc dsiMyDist(): BaseDist {
    halt("internal error: dsiMyDist is not implemented");
    return nil;
  }

  pragma "dont disable remote value forwarding"
  proc destroyDom(arr: BaseArr = nil) {
    if arr then remove_arr(arr);
    var cnt = _domCnt.fetchSub(1)-1;
    if !noRefCount {
      if cnt < 0 then
        halt("domain reference count is negative!");
    } else {
      if cnt > 0 then
        halt("domain reference count has been modified!");
    }
    if !noRefCount {
      if cnt == 0 && dsiLinksDistribution() {
        var dist = dsiMyDist();
        on dist {
          var cnt = dist.destroyDist(this);
          if cnt == 0 then
            delete dist;
        }
      }
    }
    return cnt;
  }

  inline proc remove_arr(x) {
    on this {
      _lock_arrs();
      _arrs.remove(x);
      _unlock_arrs();
    }
  }

  inline proc add_arr(x) {
    on this {
      _lock_arrs();
      _arrs.append(x);
      _unlock_arrs();
    }
  }

  inline proc _lock_arrs() {
    // WARNING: If you are calling this function directly from
    // a remote locale, you should consider wrapping the call in
    // an on clause to avoid excessive remote forks due to the
    // testAndSet()
    while (_arrsLock.testAndSet()) do chpl_task_yield();
  }

  inline proc _unlock_arrs() {
    _arrsLock.clear();
  }

  // used for associative domains/arrays
  proc _backupArrays() {
    for arr in _arrs do
      arr._backupArray();
  }

  proc _removeArrayBackups() {
    for arr in _arrs do
      arr._removeArrayBackup();
  }

  proc _preserveArrayElements(oldslot, newslot) {
    for arr in _arrs do
      arr._preserveArrayElement(oldslot, newslot);
  }

  proc dsiSupportsPrivatization() param return false;
  proc dsiRequiresPrivatization() param return false;

  // false for default distribution so that we don't increment the
  // default distribution's reference count and add domains to the
  // default distribution's list of domains
  proc linksDistribution() param return true;

  // dynamically-dispatched counterpart of linksDistribution
  proc dsiLinksDistribution() return true;

  proc dsiDisplayRepresentation() { }
}

class BaseRectangularDom : BaseDom {
  proc dsiClear() {
    halt("clear not implemented for this distribution");
  }

  proc clearForIteratableAssign() {
    compilerError("Illegal assignment to a rectangular domain");
  }

  proc dsiAdd(x) {
    compilerError("Cannot add indices to a rectangular domain");
  }

  proc dsiRemove(x) {
    compilerError("Cannot remove indices from a rectangular domain");
  }
}

class BaseSparseDom : BaseDom {
  proc dsiClear() {
    halt("clear not implemented for this distribution");
  }

  proc clearForIteratableAssign() {
    dsiClear();
  }
}

class BaseAssociativeDom : BaseDom {
  proc dsiClear() {
    halt("clear not implemented for this distribution");
  }

  proc clearForIteratableAssign() {
    dsiClear();
  }
}

class BaseOpaqueDom : BaseDom {
  proc dsiClear() {
    halt("clear not implemented for this distribution");
  }

  proc clearForIteratableAssign() {
    dsiClear();
  }
}

//
// Abstract array class
//
pragma "base array"
class BaseArr {
  var _arrCnt: atomic int; // array reference count
  var _arrAlias: BaseArr;  // reference to base array if an alias

  proc dsiStaticFastFollowCheck(type leadType) param return false;

  proc canCopyFromDevice param return false;
  proc canCopyFromHost param return false;

  proc dsiGetBaseDom(): BaseDom {
    halt("internal error: dsiGetBaseDom is not implemented");
    return nil;
  }

  pragma "dont disable remote value forwarding"
  proc destroyArr(): int {
    var cnt = _arrCnt.fetchSub(1)-1;
    if !noRefCount {
      if cnt < 0 then
        halt("array reference count is negative!");
    } else {
      if cnt > 0 then
        halt("array reference count has been modified!");
    }
    if cnt == 0 {
      if _arrAlias {
        on _arrAlias {
          var cnt = _arrAlias.destroyArr();
          if cnt == 0 then
            delete _arrAlias;
        }
      } else {
        dsiDestroyData();
      }
    }
    if !noRefCount {
      if cnt == 0 {
        var dom = dsiGetBaseDom();
        on dom {
          var cnt = dom.destroyDom(this);
          if cnt == 0 then
            delete dom;
        }
      }
    }
    return cnt;
  }

  proc dsiDestroyData() { }

  proc dsiReallocate(d: domain) {
    halt("reallocating not supported for this array type");
  }

  proc dsiPostReallocate() {
  }

  // This method is unsatisfactory -- see bradc's commit entries of
  // 01/02/08 around 14:30 for details
  proc _purge( ind: int) {
    halt("purging not supported for this array type");
  }

  proc _resize( length: int, old_map) {
    halt("resizing not supported for this array type");
  }

  //
  // Ultimately, these routines should not appear here; instead, we'd
  // like to do a dynamic cast in the sparse array class(es) that call
  // these routines in order to call them directly and avoid the
  // dynamic dispatch and leaking of this name to the class.  In order
  // to do this we'd need to hoist eltType to the base class, which
  // would require better subclassing of generic classes.  A good
  // summer project for Jonathan?
  //
  proc sparseShiftArray(shiftrange, initrange) {
    halt("sparseGrowDomain not supported for non-sparse arrays");
  }

  proc sparseShiftArrayBack(shiftrange) {
    halt("sparseShiftArrayBack not supported for non-sparse arrays");
  }

  // methods for associative arrays
  proc clearEntry(idx) {
    halt("clearEntry() not supported for non-associative arrays");
  }

  proc _backupArray() {
    halt("_backupArray() not supported for non-associative arrays");
  }

  proc _removeArrayBackup() {
    halt("_removeArrayBackup() not supported for non-associative arrays");
  }

  proc _preserveArrayElement(oldslot, newslot) {
    halt("_preserveArrayElement() not supported for non-associative arrays");
  }

  proc dsiSupportsAlignedFollower() param return false;

  proc dsiSupportsPrivatization() param return false;
  proc dsiRequiresPrivatization() param return false;

  proc dsiSupportsBulkTransfer() param return false;
  proc doiCanBulkTransfer() param return false;
  proc doiBulkTransfer(B){ 
    halt("This array type does not support bulk transfer.");
  }

  proc dsiDisplayRepresentation() { }
  proc isBlockDist() param {return false;}
  proc isDefaultRectangular() param {return false;}
}

}
