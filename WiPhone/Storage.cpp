/*
Copyright © 2019, 2020, 2021, 2022 HackEDA, Inc.
Licensed under the WiPhone Public License v.1.0 (the "License"); you
may not use this file except in compliance with the License. You may
obtain a copy of the License at
https://wiphone.io/WiPhone_Public_License_v1.0.txt.

Unless required by applicable law or agreed to in writing, software,
hardware or documentation distributed under the License is distributed
on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/


#include "Storage.h"
#include "config.h"
#include "helpers.h"

// # # # # # # # # # # # # # # # # # # # # # # # # # # # #  PHONEBOOK CLASS  # # # # # # # # # # # # # # # # # # # # # # # # # # # #

void Phonebook::parse(char* str) {
  // TODO
  if (str!=NULL) {
    freeNull((void **) &phonebookDyn);
    phonebookDyn = str;
    phonebookLen = strlen(str);
  }
}

bool Phonebook::next() {
  if (phonebookOff >= phonebookLen) {
    return false;
  }
  if (!phonebookDyn) {
    return false;
  }
  phonebookOff++;
  while (phonebookOff+1 < phonebookLen && !(phonebookDyn[phonebookOff-1]=='\n' && phonebookDyn[phonebookOff]=='n' && phonebookDyn[phonebookOff+1]=='=')) {
    phonebookOff++;
  }
  if (phonebookOff+1 >= phonebookLen) {
    phonebookOff = phonebookLen;
    return false;
  }
  return true;
}

bool Phonebook::find(unsigned int key) {
  phonebookOff = 0;
  unsigned int pos = 1;
  while (pos < key) {
    if (!this->next()) {
      break;
    }
    pos++;
  }
  return pos == key;
}

PhonebookRecord* Phonebook::nextRecord() {
  if (phonebookDyn) {
    int skip = 0;
    PhonebookRecord* r = new PhonebookRecord();
    if (r) {
      r->parse(phonebookDyn + phonebookOff, skip);
      if (r->length()>0 && skip>0) {
        phonebookOff += skip;
        return r;
      }
      delete r;
      r = NULL;
    }
  }
  return NULL;
}

PhonebookRecord* Phonebook::firstRecord() {
  phonebookOff = 0;
  return this->nextRecord();
}

PhonebookRecord* Phonebook::findRecord(unsigned int key) {
  if (this->find(key)) {
    return this->nextRecord();
  }
  return NULL;
}

// TODO: remove, use NanoINI
bool Phonebook::reformat() {
  // Backwards compatibility reformatting
  if (1 || (phonebookDyn && strstr(phonebookDyn, "name="))) {      // simplistic rule
    // Phonebook format needs to be updated
    char* d = (char *) extMalloc(phonebookLen+1);       // allocate new copy of phonebook
    if (d) {
      // Parse and serialize all the records into the new space
      int len = 0;
      PhonebookRecord* rec = this->firstRecord();
      while (rec) {
        if (len + rec->length() <= phonebookLen) {
          len += rec->serialize(d+len);
          d[len] = '\0';      // terminate after the newline
          delete rec;
          rec = this->nextRecord();
        } else {
          // error: phonebook did not shrink
          break;
        }
      }
      if (!rec && len) {
        // Update size of d
        d = (char*) extRealloc(d, len + 1);
        if (d) {
          log_d("phonebook reformatted: %d -> %d", phonebookLen, len);
          free(phonebookDyn);
          phonebookDyn = d;
          phonebookLen = len;
          d = NULL;
        }
      } else {
        // free memory after error
        delete rec;
      }
      freeNull((void **) &d);
    }
    return true;
  }
  return false;
}

unsigned int Phonebook::addRecord(PhonebookRecord* newRec) {
  // Add serialized phonebook record, place in alphabetic order
  unsigned int newPos = 0;
  unsigned int len = newRec->length();
  if (len) {
    // Extend current phonebook
    char* p = (char *) extRealloc(phonebookDyn, phonebookLen + 1 + len);
    if (p) {
      // Find position to insert phonebook record
      int lastOff = 0;
      phonebookDyn = p;
      unsigned int pos = 1;
      PhonebookRecord* rec = this->firstRecord();
      while (rec && *rec <= *newRec) {
        lastOff = phonebookOff;
        rec = this->nextRecord();
        pos++;
      }
      // Actually insert
      if (rec) {
        // record at lastOff is "greater" than newRec
        // - move contents from lastOff by `len` bytes forward
        for (int k = phonebookLen+len; k>=lastOff+len; k--) {
          phonebookDyn[k] = phonebookDyn[k-len];
        }
        // - serialize into the middle of phonebook (no need to NUL-terminate)
        if (newRec->serialize(phonebookDyn + lastOff) == len) {
          phonebookLen += len;
          newPos = pos;
        }
      } else {
        // insert new record at the end of phonebook
        if (newRec->serialize(phonebookDyn + phonebookLen) == len) {
          phonebookLen += len;
          phonebookDyn[phonebookLen] = '\0';
          newPos = pos;
        }
      }
    }
  }
  return newPos;
}

unsigned int Phonebook::addRecord(const char* name, const char* sip) {
  unsigned int newPos = 0;
  PhonebookRecord* rec = new PhonebookRecord(name, sip);
  if (rec) {
    newPos = addRecord(rec);
    delete rec;
  }
  return newPos;
}

bool Phonebook::removeRecord(unsigned int key) {
  if (!phonebookLen || !phonebookOff) {
    return false;  // TODO: why phonebookOff here?
  }

  // Allocate memory for a new copy of phonebook      (TODO: no necessity to make a new copy)
  char* copy = (char*) extMalloc(phonebookLen+1);
  if (!copy) {
    return false;
  }

  // Find the record by position/key (1..N)
  bool succ = false;
  unsigned int pos = 1;
  phonebookOff = 0;
  while (pos < key) {
    if (!this->next()) {
      break;
    }
    pos++;
  }

  // Found?
  if (pos == key) {

    // Copy the part before record
    unsigned int newLen = phonebookOff;
    if (newLen) {
      strncpy(copy, phonebookDyn, newLen);
    }

    // Skip the record
    unsigned int off0 = phonebookOff;
    if (this->next()) {
      // Copy the part after the record
      strncpy(copy + newLen, phonebookDyn + phonebookOff, phonebookLen - phonebookOff);
      newLen += phonebookLen - phonebookOff;
    }

    // Terminate string
    copy[newLen] = '\0';

    // Shrink memory usage
    char* p = (char *) extRealloc(copy, newLen+1);
    if (p) {
      // Replace phonebook
      phonebookDyn = p;
      phonebookLen = newLen;
      copy = NULL;

      succ = true;
    }
  }

  freeNull((void **) &copy);
  return succ;
}

unsigned int Phonebook::replaceRecord(unsigned int pos, const char* name, const char* sip) {
  if (!phonebookDyn || !phonebookLen || !pos || (!name && !sip)) {
    return 0;
  }

  // Make a "backup" copy of a phonebook
  char* backup = (char *) extMalloc(phonebookLen+1);
  int backupLen = phonebookLen;
  if (!backup) {
    return false;
  }
  strncpy(backup, phonebookDyn, phonebookLen);

  // Remove & insert
  unsigned int newPos = 0;
  if (this->removeRecord(pos)) {
    this->show();
    newPos = this->addRecord(name, sip);
  }

  // Remove second copy of phonebook
  if (newPos > 0) {
    freeNull((void **) &backup);
  } else {
    // error: restoring original phonebook value
    freeNull((void **) &phonebookDyn);
    phonebookDyn = backup;
    phonebookLen = backupLen;
  }
  return newPos;
}

void Phonebook::show() {
#ifdef DEBUG_MODE
  log_d("phonebookLen = %d, strlen = %d", phonebookLen, phonebookLen ? strlen(phonebookDyn) : -1);
  log_d("PHONEBOOK:");
  if (phonebookLen > 0) {
    bool succ;
    uint32_t last = phonebookOff = 0;
    do {
      succ = this->next();
      char* s = strndup(phonebookDyn + last, phonebookOff-last);
      log_d("%s", s);
      free(s);
      last = phonebookOff;
    } while (succ);
  }
#endif
}

// # # # # # # # # # # # # # # # # # # # # # # # # # # # #  MESSAGES CLASS  # # # # # # # # # # # # # # # # # # # # # # # # # # # #

MessageData::MessageData(NanoIni::Section& message) : NanoIni::Section(message) {}

MessageData::MessageData(NanoIni::Section& message, int partn) : NanoIni::Section(message) {
  (*this)["p"] = partn;
}

MessageData::MessageData(const char* fromUri, const char* toUri, const char* text, uint32_t time, bool incoming)
  : NanoIni::Section() {
  // NOTE: similar code is in Messages::saveMessage
  this->putValueFullHex("t", time);
  if (fromUri) {
    (*this)[incoming ? "o" : "s"] = fromUri;
  }
  if (toUri) {
    (*this)[incoming ? "s" : "o"] = toUri;
  }
  if (text) {
    if (NanoIni::isSafeString(text)) {
      (*this)["m"] = text;
    } else {
      this->putValueBase64("b", text);
    }
  }
}

const char* MessageData::getMessageText() {
  if (this->decodedText.size()) {
    return this->decodedText.c_str();
  } else if (this->hasKey("m")) {
    return (*this)["m"];
  } else if (this->hasKey("b")) {
    this->decodedText = this->getValueBase64("b", "");
    return this->decodedText.c_str();
  }
  return "";
}

unsigned long MessageData::getTime() {
  if (!this->time) {
    this->time = this->getHexValueSafe("t", 1);  // 1 - to avoid repeating unhexing
  }
  return this->time;
}

unsigned long MessageData::getAckTime() {
  if (!this->ackTime) {
    this->ackTime = this->getHexValueSafe("t", 1);  // 1 - to avoid repeating unhexing
  }
  return this->ackTime;
}

Messages::Messages()
  : index(indexFile) {
  preloadedRangeStart = 0;
  preloadedRangeEnd = 0;
};

bool Messages::load(uint32_t unixTime) {
  // TODO: make sure not to mess things up with unixTime if it is zero

  // Load index file
  this->loaded = false;
  if (index.load() && !index.isEmpty()) {
    // Check version of the file format
    if (index[0].hasKey("v") && !strcmp(index[0]["v"], "1")) {
      log_v("Messages index found");
      IF_LOG(VERBOSE)
      index.show();
      this->loaded = true;
    } else {
      log_e("Messages index file corrup or unknown format");
      IF_LOG(VERBOSE)
      index.show();
      return false;
    }
  } else {
    log_d("creating Messages index file");
    index[0]["desc"] = "WiPhone messages index";
    index[0]["v"] = "1";
    index.store();
    this->loaded = true;
  }

  // Check correctness

  bool removePart = false;
  bool indexUpdated = false;
  bool defDirToggle = false;      // used to initialize empty partitions
  for (auto ipart = index.iterator(1); ipart.valid(); ++ipart) {       // traverse all partitions

    // TODO: mark old empty partitions for deletion

    // Ensure that all partitions have message count
    int32_t n = ipart->getIntValueSafe("n", -1);
    if (n < 0) {
      // No message count: load the partition file
      IniFile ini;
      if (this->loadPartition(ini, ipart->getIntValueSafe("p", -1)) && !ini.isEmpty()) {
        // Updage message count in the index
        n = ini.nSections() - 1;
        (*ipart)["n"] = n;
        log_v("messages found: %d, partition: %s", n, (*ipart).getValueSafe("p", ""));
        ini.store();
        indexUpdated = true;
      } else {
        if (!ini.isLoaded()) {
          log_e("partition %s not found", ipart->getValueSafe("p", "\"\""));
          (*ipart)["remove"] = "1";   // TODO
          removePart = true;
        } else {
          log_e("partition %s is empty", ipart->getValueSafe("p", "\"\""));
        }
      }
    }

    // Ensure that all partitions have time fields in the index
    if (!ipart->hasKey("t1") || !ipart->hasKey("t2")) {
      if (ipart->hasKey("t2")) {
        log_d("t2 present, but not t1");
      } else if (ipart->hasKey("t2")) {
        log_d("t1 present, but not t2");
      } else {
        log_d("t1 & t2 absent");
      }
      // Load partition file, find min/max time, update
      IniFile ini;
      if (this->loadPartition(ini, ipart->getIntValueSafe("p", -1)) && ini.nSections() > 1) {
        if (!(ini[0].hasKey("t1") && ini[0].hasKey("t2"))) {
          // Traverse all messages to find min/max time
          const char* t1 = NULL;
          const char* t2 = NULL;
          for (auto im = ini.iterator(1); im.valid(); ++im) {
            const char* t = im->getValueSafe("t", NULL);
            if (!t) {
              // this message has no time -> set to current minute
              im->putValueFullHex("t", unixTime);
              t = im->getValueSafe("t", NULL);
            } else if (strlen(t)<8) {
              // this value must have been modified by a human -> align
              char buff[9];
              memset(buff, '0', sizeof(buff)-1);
              buff[8] = '\0';
              strcpy(buff + 8 - strlen(t), t);
              (*im)["t"] = buff;
            }
            // Find minimum
            t1 = (t1 && strncasecmp(t, t1, 8) < 0 || !t1) ? t : t1;
            // Find maximum
            t2 = (t2 && strncasecmp(t, t2, 8) > 0 || !t2) ? t : t2;
          }
          // Update partition time range
          if (t1 && t2) {
            ini[0]["t1"] = t1;
            ini[0]["t2"] = t2;
            ini.store();
          }
        }
      } else if (ini.isLoaded()) {
        // if partition file is empty for some reason (should not happen) -> assign it current time (as it must be a "lost" new partition)
        if (ini.isEmpty() || !ini[0].hasKey("v") && !ini[0].hasKey("desc")) {
          // if we are here, something is either terribly wrong and/or files were edited by a user
          if (ini.isEmpty()) {
            ini.addSection();
          } else {
            ini[0].remove(nullptr);  // remove values with empty key
          }
          log_e("initializing partiton");
          ini[0]["desc"] = "WiPhone messages partition";
          ini[0]["v"] = "1";
          ini[0]["d"] = ipart->getValueSafe("d", (defDirToggle = !defDirToggle) ? "i" : "o");
        }
        log_e("partiton file has no messages");
        ini[0].putValueFullHex("t1", unixTime);
        ini[0]["t2"] = ini[0]["t1"];
        if (!ini[0].hasKey("p") && ipart->hasKey("p")) {
          ini[0]["p"] = (*ipart)["p"];
        }
        ini.store();
      } else {
        // Partition file does not exist and shoud be deleted from the index
        (*ipart)["remove"] = "1";
        removePart = true;
      }

      // Update index
      log_d("updating index");
      (*ipart)["t1"] = ini[0].getValueSafe("t1", "386d4380");      // default = 2000-01-01
      (*ipart)["t2"] = ini[0].getValueSafe("t2", "386d4380");
      indexUpdated = true;
    }
  }
  if (indexUpdated || removePart) {
    if (removePart) {
      for (int32_t i = 1; i < index.nSections();) {       // traverse all partitions
        if (index[i].hasKey("remove")) {
          log_e("removing partition %d from index", i);
          index.removeSection(i);
        } else {
          i++;
        }
      }
    }
    index.store();
  }

  // Sort partitions by time in descending order
  log_v("sorting index");
  index.sortFrom(1, Storage::messagePartitionCompare);

  IF_LOG(VERBOSE)
  index.show();

  return true;
}

/* Description:
 *     clear messages data from cache (and, as a consequence, free memory currently occupied by cache)
 */
void Messages::unload() {
  this->clearPreloaded();
  this->index.unload();
  if (this->part1.isLoaded()) {
    this->part1.unload();
  }
  if (this->part2.isLoaded()) {
    this->part2.unload();
  }
}

int32_t Messages::inboxTotalSize() {
  return this->countAll(INCOMING);
}

int32_t Messages::sentTotalSize() {
  return this->countAll(SENT);
}

/*
 * Description:
 *     Return current count of incoming/sent messages.
 *     If no such number is recorded in the database, parse partition files one-by-one if necessary.
 */
int32_t Messages::countAll(bool incoming) {
  int32_t cnt = 0;
  bool indexChange = false;
  for (auto ipart = index.iterator(1); ipart.valid(); ++ipart) {
    if (!ipart->hasKey("d")) {
      log_e("partition description without `d` key %d", (int32_t)ipart);
      continue;
    }
    if (strcmp(ipart->getValueSafe("d",""), incoming ? "i" : "o")) {
      continue;
    }
    int32_t n = ipart->getIntValueSafe("n", -1);
    if (n>=0) {
      cnt += n;  // avoid negative count at all cost
    }
  }
  return cnt;
}

void Messages::clearPreloaded() {
  log_v("clearing preloaded");
  for (auto it = preloaded.iterator(); it.valid(); ++it)
    if (*it != nullptr) {
      delete (*it);
    }
  preloaded.clear();      // frees memory
  this->preloadedRangeStart = this->preloadedRangeEnd = 0;
}

/*
 * Description:
 *     Resolve up to two partitions and make sure `count` of messages from position `offset` are pre-loaded
 *     to an in-memory NanoIni structure (`MessagesArray preloaded`).
 * Parameters:
 *     This method accepts negative offsets (in Python style).
 * Return:
 *     Number of messages actually preloaded (in addition to those, that happened to be preloaded already).
 */
int32_t Messages::preload(bool incoming, int32_t offset, int32_t count) {
  log_i("incoming? %d / offset: %d / count: %d / range: %d..%d", incoming, offset, count, preloadedRangeStart, preloadedRangeEnd);

  if (incoming != preloadedIncoming || preloadedRangeStart == preloadedRangeEnd ||
      ((offset >= 0) ? (offset < preloadedRangeStart || offset > preloadedRangeEnd) : (offset > preloadedRangeStart || offset < preloadedRangeEnd))) {
    // Range needs to be reloaded entirely
    this->clearPreloaded();
    this->preloadedRangeStart = this->preloadedRangeEnd = offset;
    this->preloadedIncoming = incoming;
  } else {
    // Partially loaded already?
    int32_t diff = this->preloadedRangeEnd - offset;
    offset += diff;
    if (offset < 0) {
      diff = -diff;
    }
    count -= diff;
    if (count <= 0) {
      log_v("range fully loaded already");
      return 0;   // fully loaded already
    }
  }

  // Load needed partitions
  if (offset >= 0) {
    // currently this assumes that offset is negative (we are counting from the end)
    log_e("positive offsets not implemented");
    return 0;
  }
  int32_t skip = -offset - 1;         // how many message to skip in the database? offset == -1 -> 0, -2 -> 1, ...
  int32_t skipFirst = 0;              // how many messages to skip in the first partition?
  bool firstPartFound = false;
  for (auto ipart = index.iterator(1); ipart.valid(); ++ipart) {      // traverse index for partitions
    ipart->show();
    // Choose partitions according to `incoming`
    log_d("d=%s", ipart->getValueSafe("d",""));
    if (strcmp(ipart->getValueSafe("d",""), incoming ? "i" : "o")) {
      continue;
    }

    // Get number of messages in the partition
    log_d("number of messages n=%s, skip=%d", ipart->getValueSafe("n",""), skip);
    int32_t n = ipart->getIntValueSafe("n", 0);

    if (skip < n && !firstPartFound) {
      // First partition located
      skipFirst = skip;
      int32_t partn = ipart->getIntValueSafe("p", -1);
      log_v("located first partition %d", partn);
      firstPartFound = true;
      if (partn < 0) {
        log_e("illegal partition number: %s", ipart->getValueSafe("p", ""));
      } else if (!part1.isLoaded() || part1.isEmpty() || part1[0].getIntValueSafe("p", -2) != partn) {
        this->loadPartition(this->part1, partn);
        // If we reloaded first partition, second partition must be also irrelevand if it's loaded
        part2.unload();
        if (skip + count <= n) {
          log_v("only one partition needed");
          break;   // no need to load next partition
        }
      }
    } else if (firstPartFound) {
      // Second partition directly follows first partition (if it is non-empty)
      if (n <= 0) {
        continue;
      }
      int32_t partn = ipart->getIntValueSafe("p", -1);
      log_v("located second partition %d", partn);
      if (partn < 0) {
        log_e("illegal partition number: %s", ipart->getValueSafe("p", ""));
      } else if (!part2.isLoaded() || part2.isEmpty() || part2[0].getIntValueSafe("p", -2) != partn) {
        this->loadPartition(part2, partn);
      }
      break;
    }
    skip -= n;
  }
  if (!firstPartFound) {
    log_d("nothing to load");
    return 0;
  }
  if (!part1.isLoaded() || part1.isEmpty()) {
    log_e("part1 not loaded or empty");
    return 0;
  }

  // Preload messages from the located partition(s)
  preloaded.ensure(abs(preloadedRangeEnd - preloadedRangeStart) + count);
  int32_t cnt = 0;
  log_d("loading from part1 %d", skipFirst);
  int partn = part1[0].getIntValueSafe("p", -1);
  for (auto im = part1.iterator(1+skipFirst); im.valid() && cnt < count; ++im) {
    // WARNING: repeating code below
    preloaded.add(new MessageData(*im, partn));      // index == abs(preloadedRangeEnd - preloadedRangeStart)
    preloadedRangeEnd += (offset < 0) ? -1 : 1;
    cnt++;
  }
  if (part2.isLoaded() && cnt < count) {
    partn = part1[0].getIntValueSafe("p", -1);
    log_d("loading from part2");
    for (auto im = part2.iterator(1); im.valid() && cnt < count; ++im) {
      // WARNING: repeating code from above
      preloaded.add(new MessageData(*im, partn));
      preloadedRangeEnd += (offset < 0) ? -1 : 1;
      cnt++;
    }
  }
  log_i("preloaded: %d, from: %d, to: %d", cnt, preloadedRangeStart, preloadedRangeEnd);
  return cnt;
}

bool Messages::loadPartition(IniFile& ini, int32_t part) {
  log_d("loadPartition %d", part);
  if (part < 0) {
    log_e("invalid partition number: %d", part);
    return false;
  }
  char fn[20];
  snprintf(fn, sizeof(fn), partitionFileFormat, part);
  log_v("messages file: %s", fn);
  return ini.load(fn);
}

/* Description:
 *     store message in the message database.
 *     Encode message in Base64 if it has non-printable characters or newlines.
 * Return:
 *     hash of the message text (or message data).
 */
Messages::hash_t Messages::saveMessage(const char* text, const char* fromUri, const char* toUri,
                                       bool incoming, unsigned long time, unsigned long ackTime) {
  log_v("saving message to %s, time = %d, d = %c", toUri ? toUri : "nil", time, incoming ? 'i' : 'o');

  if (!time) {
    time--;  // store 0xFFFFFFFF insted of 0x00000000 so that sorting is still correct
  }

  // Find first partition of the right type in index
  int32_t msgcnt = -1;
  int32_t partn  = -1;
  int partPos = 0;
  bool io = false;
  index.show();
  for (auto ipart = index.iterator(1); ipart.valid(); ++ipart) {       // traverse all partitions (start at section 1 because we want to skip 0, the header partition)
    if (strchr(ipart->getValueSafe("d", ""), incoming ? 'i' : 'o')) {
      msgcnt = ipart->getIntValueSafe("n", -1);
      partn  = ipart->getIntValueSafe("p", -1);
      partPos = (int32_t)ipart;
      if (strchr(ipart->getValueSafe("d", ""), incoming ? 'o' : 'i')) {
        io = true;
      }
      break;
    }
  }

  // Load or create the partition
  IniFile ini;
  bool inited = false;
  if (partn >=0 && msgcnt < Messages::PARTITION_SIZE) {

    log_d("selected partition: %d", partn);
    // Non-filled partition found -> add message into it
    this->loadPartition(ini, partn);
    log_d("partition loaded: %d", partn);

  } else {
    log_d("creating new partition");
    // Else -> create new partition

    // Determine the new partition number
    partn = index[0].getIntValueSafe("x", 1);
    for (auto ipart = index.iterator(1); ipart.valid(); ++ipart) {       // traverse all partitions
      int pn = ipart->getIntValueSafe("p", 0) + 1;
      if (pn > partn) {
        partn = pn;
      }
    }
    log_d("partn = %d", partn);

    // Assign partition filename, make sure the file does not exist
    char filename[20];
    do {
      snprintf(filename, sizeof(filename), partitionFileFormat, partn);
      log_v("messages file: %s", filename);
      if (ini.load(filename)) {
        log_e("file exists");
        partn++;
      }
    } while (ini.isLoaded());

    // Initialize partition
    ini.addSection();
    ini[0]["desc"] = "WiPhone messages partition";
    ini[0]["v"] = "1";
    ini[0]["n"] = "1";
    ini[0]["d"] = incoming ? "i" : "o";
    ini[0].putValueFullHex("t1", time);
    ini[0]["t2"] = ini[0]["t1"];
    ini[0]["p"] = partn;
    inited = true;
  }

  if (!ini.isLoaded() && !inited) {
    log_e("no partition to store");
    return 0;
  }

  // Actually store message into the partition
  log_d("storing new message: %d", partn);
  ini.addSection();
  ini[-1].putValueFullHex("t", time);
  if (fromUri) {
    ini[-1][incoming ? "o" : "s"] = fromUri;
  }
  if (toUri) {
    ini[-1][incoming ? "s" : "o"] = toUri;
  }
  if (ackTime) {
    ini[-1].putValueFullHex("a", ackTime);
  }
  if (io) {
    ini[-1]["d"] = incoming ? "i" : "o";
  }
  if (incoming) {
    ini[-1]["u"] = "1";  // this message was not read yet
  }
  if (text) {
    if (NanoIni::isSafeString(text)) {
      ini[-1]["m"] = text;
    } else {
      ini[-1].putValueBase64("b", text);
    }
  }
  ini.reorderLast(1, Storage::messageCompare);
  if (incoming) {
    ini[0]["u"] = ini[0].getIntValueSafe("u", 0) + 1;  // increase the unread messages counter for entire partition
  }

  // Store partition
  if (ini.store()) {

    // Update index
    log_d("updating index");
    if (inited) {

      // Register new partition
      log_d("register new partition: %d", partn);
      index.addSection();
      index[-1]["p"] = partn;
      index[-1]["d"] = incoming ? "i" : "o";
      index[-1]["n"] = "1";
      index[-1].putValueFullHex("t1", time);
      index[-1]["t2"] = index[-1]["t1"];
      partPos = -1;

    } else if (partPos!=0) {

      // Update existing index section
      log_d("updated existing index");
      index[partPos]["n"] = index[partPos].getIntValueSafe("n", 0) + 1;
      auto tt = index[partPos].getHexValueSafe("t1", 2);
      if (time < tt) {
        index[partPos].putValueFullHex("t1", time);
      }
      tt = index[partPos].getHexValueSafe("t2", 2);
      if (time > tt) {
        index[partPos].putValueFullHex("t2", time);
      }
    }

    // Update unread counters in the index
    if (incoming) {
      index[partPos]["u"] = ini[0]["u"];
      index[0]["u"] = index[0].getIntValueSafe("u", 0) + 1;         // increase the unread messages counter for entire database
    }

    // Restore sorting by time in the index
    log_v("sorting index");
    index.sortFrom(1, Storage::messagePartitionCompare);

    if (!index.store()) {
      log_e("failed to save index");
    }

  } else {
    log_e("failed to save appended partition");
  }
  return 0;
}

bool Messages::findMessage(MessageData& msg, IniFile& part, int32_t& section) {
  log_i("<-- Messages");

  // Check if messages has all the necessary fields
  if (!msg.hasKey("m") && !msg.hasKey("b") || !msg.hasKey("t") || !msg.hasKey("p")) {
    log_e("missing field(s) in MessageData");
    IF_LOG(ERROR) msg.show();
    return false;
  }

  int partn = msg.getIntValueSafe("p", -1);
  log_v("looking for partition: %d", partn);

  // Ensure the message partition is loaded
  part = part1;
  if (!part.isLoaded() || part.isEmpty() || part[0].getIntValueSafe("p", -2) != partn) {
    part = part2;
    if (!part.isLoaded() || part.isEmpty() || part[0].getIntValueSafe("p", -2) != partn) {
      log_v("loading partition %d", partn);
      part = part1;
      this->loadPartition(part, partn);
    }
  }

  // Find the actual message in the partition by time ("t") and text ("m")
  if (part.isLoaded() && !part.isEmpty()) {
    const char* field = msg.hasKey("m") ? "m" : "b";
    section = part.query("t", msg["t"], field, msg[field]);
    return section > 0;
  }
  log_e("not found");
  return false;
}

/* Description:
 *      take negative message offset (global message offset in the database), find the message in the cache (`preloaded` array)
 *      and pass it for deletion.
 */
bool Messages::deleteMessage(int32_t messageOffset) {
  log_i("messageOffset = %d", messageOffset);
  if (messageOffset <= preloadedRangeEnd || messageOffset > preloadedRangeStart) {
    log_e("wrong message offset %d, not in |%d..%d>", messageOffset, preloadedRangeStart, preloadedRangeEnd);
    return false;
  }
  messageOffset = abs(messageOffset - preloadedRangeStart);
  log_v("delete: preloaded[%d]", messageOffset);
  return this->deleteMessage(*preloaded[messageOffset]);
}

/* Description:
 *     find message by partition, text and time and delete it. Remove partition if it becomes empty as a result.
 * Return:
 *     true on success
 */
bool Messages::deleteMessage(MessageData& msg) {
  log_i("deleting message");
  IF_LOG(VERBOSE)
  msg.show();

  // Find message
  IniFile& ini = part1;
  int key = 0;
  bool found = this->findMessage(msg, ini, key);
  log_v("found == %d", found);

  // Remove message and update index
  if (found && key > 0) {

    // Remove message section from the partition
    ini.removeSection(key);

    // Update message counter in the index
    int partn = msg.getIntValueSafe("p", -1);
    int section = index.query("p", partn);    // key of the partition inside index
    index[section]["n"] = (int32_t)(ini.nSections() - 1);

    if (ini.nSections() > 1) {

      // There are more messages left in this partition -> only update times

      // Find min/max time
      uint32_t t1 = 0xFFFFFFFF, t2 = 0;
      for (auto it = ini.iterator(1); it.valid(); ++it) {
        if (!it->hasKey("t")) {
          continue;
        }
        uint32_t t = it->getHexValueSafe("t", 0);
        if (t < t1) {
          t1 = t;
        }
        if (t > t2) {
          t2 = t;
        }
      }

      if (ini[0].getHexValueSafe("t1", 0xFFFFFFFF) != t1 || ini[0].getHexValueSafe("t2", 0) != t2) {

        // Update partition times

        // - in partition file
        ini[0].putValueFullHex("t1", t1);
        ini[0].putValueFullHex("t2", t2);
        // - in index file
        if (section > 0) {
          index[section].putValueFullHex("t1", t1);
          index[section].putValueFullHex("t2", t2);
        }

      }

      // Save partition
      ini.store();

    } else {

      // No messages left in this partition -> remove the partition

      // Remove partition file
      ini.remove();

      // Remove partition section
      index.removeSection(section);

    }

    // Restore sorting by time in the index
    log_v("sorting index");
    index.sortFrom(1, Storage::messagePartitionCompare);

    // Save index
    index.store();

    return true;
  }
  return false;
}

void Messages::setRead(MessageData& msg) {
  if (!msg.hasKey("u")) {
    log_e("message already read");
    return;
  }

  // Change "read" state in the preloaded array
  msg.setRead();

  // Find message
  IniFile& ini = part1;
  int key = 0;
  bool found = this->findMessage(msg, ini, key);

  // Change unread ("u") fields
  if (found && key > 0) {
    // Remove unread flag from the message
    ini[key].remove("u");

    // Update unread messages counters
    // - unread counter in this partition
    int unread = ini[0].getIntValueSafe("u", 1) - 1;
    if (unread) {
      ini[0]["u"] = unread;
    } else {
      ini[0].remove("u");
    }
    // - unread counter in the index for this partition
    int partn = msg.getIntValueSafe("p", -1);
    key = index.query("p", partn);    // key of the partition inside index
    if (key>0) {
      if (unread) {
        index[key]["u"] = unread;
      } else {
        index[key].remove("u");
      }
    }
    // - unread counter of the index
    int globalUnread = index[0].getIntValueSafe("u", unread+1) - 1;
    if (globalUnread) {
      index[0]["u"] = unread;
    } else {
      index[0].remove("u");
    }

    // Save files to flash
    ini.store();
    index.store();
  } else {
    log_e("message not found");
  }
}

void Messages::setSent(MessageData& msg) {
  // TODO
}

// # # # # # # # # # # # # # # # # # # # # # # # # # # # #  STORAGE CLASS  # # # # # # # # # # # # # # # # # # # # # # # # # # # #

int Storage::messagePartitionCompare(NanoIni::Section** a, NanoIni::Section** b) {
  int res = strcasecmp((*b)->getValueSafe("t2", ""), (*a)->getValueSafe("t2", ""));
  if (!res) {
    res = strcasecmp((*b)->getValueSafe("t1", ""), (*a)->getValueSafe("t1", ""));
  }
  if (!res) {
    res = strcasecmp((*b)->getValueSafe("p", ""), (*a)->getValueSafe("p", ""));
  }
  return res;
}

int Storage::messageCompare(NanoIni::Section** a, NanoIni::Section** b) {
  return strcasecmp((*b)->getValueSafe("t", ""), (*a)->getValueSafe("t", ""));
}

// - - - - - - - - - - - - - - - - - - - - -  Phonebook  - - - - - - - - - - - - - - - - - - - - -

int Storage::phonebookCompare(NanoIni::Section** a, NanoIni::Section** b) {
  int res = strcasecmp((*a)->getValueSafe("n", ""), (*b)->getValueSafe("n", ""));
  if (!res) {
    res = strcasecmp((*a)->getValueSafe("s", ""), (*b)->getValueSafe("s", ""));
  }
  return res;
}

bool Storage::loadPhonebook() {
  if ((phonebook.load() || phonebook.restore()) && !phonebook.isEmpty()) {
    if (phonebook[0].hasKey("v") && !strcmp(phonebook[0]["v"], "2")) {    // check version of the file format
      log_v("phonebook loaded");
      return true;
    } else {
      log_d("phonebook format error");
      return false;
    }
  } else if (this->loadPhonebookOld()) {
    // Migrate phonebook data to an INI file
    log_d("reformatting phonebook from NVS to INI:");
    phonebook.addSection();
    phonebook[0]["desc"] = "WiPhone phonebook";
    phonebook[0]["v"] = "2";
    int sect = 1;
    for (PhonebookRecord* rec = phonebookOld.firstRecord(); rec != NULL; rec = phonebookOld.nextRecord()) {
      phonebook.addSection();
      phonebook[sect]["n"] = rec->getName();
      phonebook[sect]["s"] = rec->getSip();
      sect++;
    }
    log_d("new phonebook:");
    phonebook.store();
    phonebook.backup();
    return true;
  } else {
    log_d("phonebook not found");
    phonebook[0]["desc"] = "WiPhone phonebook";
    phonebook[0]["v"] = "2";
    return true;
  }
}

bool Storage::loadPhonebookOld() {
  log_d("loadPhonebookOld");

  this->end();
  this->begin("addr");
  bool succ = true;
  {
    // Load phonebook from NVS

    // Load the "max" value
    uint16_t addrMaxId = this->getUShort("max",  0);
    log_d("maxId: %d", addrMaxId);

    // Load individual "subpages" and merge them
    char* strDyn = NULL;
    int strLen = 0;
    for (uint16_t key=1; key<=addrMaxId; key++) {
      char addrId[6];   // up to 5 decimal numbers
      sprintf(addrId, "%d", key);
      // if success (any bytes) -> merge
      char buff[Storage::nvsMaxStringLen+2];
      uint16_t bytes = this->getString(addrId,  buff, sizeof(buff)-1);
      if (bytes) {
        // merge into single phonebook (in PSRAM)
        int len = strlen(buff);
        if (len && buff[len-1]!='\n') {
          buff[len++] = '\n';  // terminate string with NL (backwards compatibility)
          buff[len] = '\0';
        }
        char* p = (char *) extRealloc(strDyn, strLen + 1 + len);
        if (p) {
          strDyn = p;
          memcpy(strDyn + strLen, buff, len);
          strLen += len;
          strDyn[strLen] = '\0';
        } else {
          succ = false;
        }
      }
    }

    // Pass the obtained string to phonebook class
    phonebookOld.parse(strDyn);
    //freeNull((void **) &strDyn);    // TODO: why is this commented?
  }

  IF_LOG(VERBOSE)
  phonebookOld.show();

  return succ;
}

// - - - - - - - - - - - - - - - - - - - - -  IniFile class  - - - - - - - - - - - - - - - - - - - - -

IniFile::IniFile(const char* fn) {
  this->setFilename(fn);
}

IniFile::~IniFile() {
  freeNull((void **) &filenameDyn);
}

bool IniFile::load() {
  if (loaded) {
    log_e("file is already loaded");
    return false;
  }
  if (filenameDyn == nullptr || *filenameDyn=='\0') {
    log_e("could not load: filename empty");
    return false;
  }
  if (!SPIFFS.exists(filenameDyn)) {
    log_e("could not load: file \"%s\" does not exist", filenameDyn);
    return false;
  }
  File file = SPIFFS.open(filenameDyn);
  if (file && file.available()) {

    // Read entire INI file into the external RAM
    int bytes;
    char buff[1025];
    LinearArray<char, LA_EXTERNAL_RAM> fileContent;
    do {
      bytes = file.readBytes(buff, sizeof(buff)-1);
      if (bytes > 0) {
        fileContent.extend(buff, bytes);  // NOTE: this creates small unnecessary overhead for small files
      }
    } while (bytes == sizeof(buff)-1);
    log_d("Read %d bytes from \"%s\"", fileContent.size(), filenameDyn);
    fileContent.add('\0');    // terminate with nul-character to form a C-string

    // Parse INI
    if (fileContent.size() > 1) {
      log_v("INI file before parsing:\n%s", &fileContent[0]);
      this->parse(&fileContent[0]);
      this->loaded = true;
    }
    return true;
  } else {
    log_e("could not load or empty: file \"%s\"", filenameDyn);
    return false;
  }
}

bool IniFile::load(const char* filename) {
  this->setFilename(filename);
  return this->load();
}

void IniFile::setFilename(const char* filename) {
  if (this->loaded) {
    this->unload();
  }
  freeNull((void **) &filenameDyn);
  filenameDyn = extStrdup(filename);
}

/* Description:
 *     unload from RAM
 */
void IniFile::unload() {
  this->clear();
  this->loaded = false;
}

bool IniFile::store() {
  if (filenameDyn == nullptr || *filenameDyn=='\0') {
    log_e("could not store: filename empty");
    return false;
  }
  log_d("writing file \"%s\"", filenameDyn);
  File file = SPIFFS.open(filenameDyn, FILE_WRITE);
  if (file) {
    size_t len = this->length();
    char* str = (char*) extMalloc(len+1);
    this->sprint(str);
    log_v("writing file:\n%s", str);            // prints entire file contents to logs
    log_v("-------------\nfile size: %d", len);
    file.write((const uint8_t*) str, len);
    file.close();
    free(str);
    log_v("wrote %d bytes to \"%s\"", len, filenameDyn);
    loaded = true;    // in-memory data corresponds to disk data
    return true;
  } else {
    log_e("failed to create a file");
    return false;
  }
}

void IniFile::show() {
  log_d("IniFile \"%s\" contents:\r\n%s", filenameDyn, this->p_c_str().get());
}

void IniFile::remove() {
  SPIFFS.remove(filenameDyn);
  this->clear();
}

// - - - - - - - - - - - - - - - - - - - - -  CriticalFile class  - - - - - - - - - - - - - - - - - - - - -

/* Description:
 *     try to save the NanoINI string into the NVS on backup() (but as long as it's not already stored)
 */
bool CriticalFile::backup(uint32_t unixTime) {
  // Get NVS page name
  char* pageDyn = this->pagename();
  if (!pageDyn) {
    log_e("empty pagename, cannot backup");
    return false;
  }
  bool success = false;

  // Check length
  size_t len = this->length();
  if (len > 0 && len < 4000) {

    // Load the backup string and compute hash
    HASHHEX storedHash;
    storedHash[0] = '\0';     // initialize as an empty string
    this->end();
    if (this->begin(pageDyn, true)) {      // read-only
      const int maxStringSize = 4001;          // including the nul-character
      char* tmp = (char*) extMalloc(maxStringSize);
      if (tmp!=NULL) {
        int len1 = this->getString(backupKey, tmp, maxStringSize);
        if (len1 > 0 && len1 != maxStringSize) {
          md5Compress(tmp, len1-1, storedHash);
        }

      }
    }

    // Serialize this INI
    char* str = (char*) extMalloc(len+1);
    this->sprint(str);

    // Calculate hash to compare to the previous one
    if (storedHash[0] != '\0') {
      HASHHEX thisHash;
      md5Compress(str, len, thisHash);
      if (!strcmp(storedHash, thisHash)) {
        log_i("%d bytes to \"%s\": same data, skipping", len, pageDyn);
        success = true;
      }
    }

    // String is sufficiently short and differs from the already strored -> save to NVS
    if (!success) {
      this->end();
      if (this->begin(pageDyn, false)) {      // not read-only
        if (this->putString(backupKey, str)>0) {
          log_i("%d bytes to \"%s\": successful", len, pageDyn);
          success = true;
        }
      }
    }
  }

  if (!success) {
    log_e("%d bytes to \"%s\": FAILED", len, pageDyn);
  }
  freeNull((void **) &pageDyn);
  return success;
}

/* Description:
 *     restore NanoINI file from the NVS
 */
bool CriticalFile::restore() {
  log_d("restoring INI");

  // Get NVS page name
  char* pageDyn = this->pagename();
  if (!pageDyn) {
    log_e("empty NVS page name, cannot restore");
    return false;
  }
  bool success = false;

  // Load from NVS
  this->end();
  if (this->begin(pageDyn, true)) {         // read-only
    const int maxStringSize = 4001;         // including the nul-character
    char* tmp = (char*) extMalloc(maxStringSize);
    if (tmp != NULL) {
      int len1 = this->getString(backupKey, tmp, maxStringSize);
      if (len1 > 0 && len1 != maxStringSize) {
        this->parse(tmp);
        if (!this->isEmpty()) {
          log_i("%d bytes from \"%s\": successful", len1-1, pageDyn);
          success = true;
        }
      }
      freeNull((void **) &tmp);
    }
  }
  if (!success) {
    log_e("page \"%s\": FAILED", pageDyn);
  }
  freeNull((void **) &pageDyn);

  // Finally: save to SPIFFS
  if (success) {
    this->store();
  }

  return success;
}

void CriticalFile::showBackup() {
  log_d("showing contents of INI backup");

  // Get NVS page name
  char* pageDyn = this->pagename();
  if (!pageDyn) {
    log_e("empty NVS page name, cannot restore");
    return;
  }
  bool success = false;

  // Load from NVS
  this->end();
  if (this->begin(pageDyn, true)) {         // read-only
    const int maxStringSize = 4001;         // including the nul-character
    char* tmp = (char*) extMalloc(maxStringSize);
    if (tmp != NULL) {
      int len1 = this->getString(backupKey, tmp, maxStringSize);
      if (len1 > 0) {
        log_i("%d bytes in backup", len1-1);
        log_i("contents:\n%s", tmp);
        success = true;
      }
      freeNull((void **) &tmp);
    }
  }
  if (!success) {
    log_e("page \"%s\": FAILED", pageDyn);
  }
  freeNull((void **) &pageDyn);
}

/* Description:
 *     return dynamically allocated string for the page name
 */
char* CriticalFile::pagename() {
  if (filenameDyn == NULL || *filenameDyn=='\0') {
    return NULL;  // empty name
  }

  // Take part following the rightmost slash
  const char* tmp = strrchr(filenameDyn, '/');
  tmp = (*tmp=='/') ? tmp + 1 : filenameDyn;

  // Take part before the first dot; if starts with the dot - take entire name
  size_t len = strcspn(tmp, ".");
  if (!len) {
    len = strlen(tmp);
  }
  if (!len) {
    return NULL;  // empty basename
  }

  return strndup(tmp, len);
}

// - - - - - - - - - - - - - - - - - - - - -  Configs for UdpSenderApp  - - - - - - - - - - - - - - - - - - - - -

void Storage::loadUdpSender(const char*& ip, int32_t& port, const char*& text) {
  char str[defaultMaxText+1];
  this->end();
  if (this->begin("app_udp_send", true)) {       // read only
    ip   = this->getString("ip",   str, sizeof(str)-1) ? strdup(str) : strdup("");
    text = this->getString("text", str, sizeof(str)-1) ? strdup(str) : strdup("");
    port = this->getInt("port", -1);    // not UShort because default value is negative
  } else {
    ip = text = NULL;
    port = -1;
  }
  this->end();
}

void Storage::storeUdpSender(const char* ip, const int32_t port, const char* text) {
  this->end();
  if (this->begin("app_udp_send", false)) {       // not read-only
    if (ip!=NULL) {
      this->putString("ip", ip);
    }
    if (text!=NULL) {
      this->putString("text", text);
    }
    this->putInt("port", port);           // not UShort because default value is negative
  }
  this->end();
}

// - - - - - - - - - - - - - - - - - - - - -  Generic helpers  - - - - - - - - - - - - - - - - - - - - -

Storage::Storage()
  : phonebook(Storage::PhonebookFile)
{};

void Storage::storeString(const char* page, const char* variable, const char* val) {
  this->end();
  if (this->begin(page, false)) {       // not read-only
    this->putString(variable, val ? val : "");      // if empty or NULL - overwrite with empty string
  }
  this->end();
}

/* Description:
 *     load a string from NVS
 * Return:
 *     dynamically allocated string is stored into val
 */
void Storage::loadString(const char* page, const char* variable, const char*& val) {
  this->end();
  if (this->begin(page, true)) {       // read only
    log_v("page found: %s", page ? page : "(nil)");
    char str[defaultMaxText+1];
    val = this->getString(variable, str, sizeof(str)-1) ? strdup(str) : strdup("");     // return empty string on error   // TODO: use str::move
    log_v("loaded: %s", str);
  } else {
    log_e("page not found: %s / %s", page ? page : "(nil)", variable ? variable : "(nil)");
    val = strdup("");
  }
  this->end();
}

void Storage::storeInt(const char* page, const char* variable, int32_t val) {
  this->end();
  if (this->begin(page, false)) {       // not read-only
    this->putInt(variable, val);
  }
  this->end();
}

void Storage::loadInt(const char* page, const char* variable, int32_t& val) {
  this->end();
  if (this->begin(page, true)) {              // read only
    val = this->getInt(variable, val);        // return what provided on error
  }
  this->end();
}

// # # # # # # # # # # # # # # # # # # # # # # # # # # # #  SUPPORT CLASSES  # # # # # # # # # # # # # # # # # # # # # # # # # # # #

// - - - - - - - - - - - - - - - - - - - - -  Phonebook record  - - - - - - - - - - - - - - - - - - - - -

PhonebookRecord::PhonebookRecord(const char* phonebook, const char* sip) {
  if (phonebook) {
    this->nameDyn = strdup(phonebook);
  }
  if (sip) {
    this->sipDyn = strdup(sip);
  }
}

PhonebookRecord::~PhonebookRecord() {
  freeNull((void **) &this->nameDyn);
  freeNull((void **) &this->sipDyn);
}

bool PhonebookRecord::operator<=(const PhonebookRecord& other) {
  if (this->nameDyn && other.nameDyn && nameDyn[0] && other.nameDyn[0]) {
    return strcasecmp(this->nameDyn, other.nameDyn) <= 0;
  }
  if (this->nameDyn && nameDyn[0]) {
    return true;
  }
  if (other.nameDyn && other.nameDyn[0]) {
    return false;
  }
  if (this->sipDyn && other.sipDyn && sipDyn[0] && other.sipDyn[0]) {
    return strcasecmp(this->sipDyn, other.sipDyn) <= 0;
  }
  if (this->sipDyn && sipDyn[0]) {
    return true;
  }
  return false;
}

void PhonebookRecord::parse(const char* str, int& skip) {
  skip = 0;
  if (str[0]=='n') {
    const char* eq = str + strcspn(str, "=");
    const char* nl = str + strcspn(str, "\n");
    if (((eq-str)==1 || ((eq-str)==4 && !strncmp(str, "name=", 5))) && nl>eq) {
      //log_d("name found");
      skip = nl-str;
      this->nameDyn = strndup(eq+1, nl-eq-1);
      if (*nl!='\0') {    // are there more fields?
        // Next field
        skip++;
        str = nl + 1;
        if (str[0] == 's') {
          eq = str + strcspn(str, "=");
          nl = str + strcspn(str, "\n");
          if (((eq-str)==1 || ((eq-str)==3 && !strncmp(str, "sip=", 4))) && nl>eq) {
            //log_d("sip found");
            skip += nl-str;
            this->sipDyn = strndup(eq+1, nl-eq-1);
            if (*nl!='\0') {
              skip++;
            }
          }
        }
      }
    }
    //log_d("PhonebookRecord: name=%s, sip=%s, skip=%d", this->nameDyn ? this->nameDyn : "", this->sipDyn ? this->sipDyn : "", skip);
  }
}

int PhonebookRecord::length() {
  int r = 6;    // n=\ns=\n
  if (this->nameDyn) {
    r += strlen(this->nameDyn);
  }
  if (this->sipDyn) {
    r += strlen(this->sipDyn);
  }
  return r;
}

int PhonebookRecord::serialize(char* buff) {
  char* start = buff;
  buff += sprintf(buff, "n=%s\n", this->nameDyn ? this->nameDyn : "");
  buff += sprintf(buff, "s=%s", this->sipDyn ? this->sipDyn : "");
  *buff++ = '\n';   // terminate with NL, not NUL
  return buff-start;
}
