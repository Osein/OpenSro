# Bugs found in the original binaries

The rewrite reproduces the original behaviour, bugs included, so it can replace the original
executables one to one. Every bug found while porting is written down here so it can be fixed
later in a separate pass. In the code each one is marked with `// BUG(<ID>)`.

One file per bug: where it is (module, address, rewrite file), what the original does, what
that causes, and a suggested fix. Severity is a rough guess: **crash** (can take the process
down), **wrong** (wrong result or lost data), **minor** (cosmetic, leak, dead code).

| ID | Area | Severity | Summary |
|---|---|---|---|
| [BSLIB-001](BSLIB-001.md) | BSLib debug | minor | Symbol search path: environment variables used only when `_dupenv_s` fails; path never used |
| [BSLIB-002](BSLIB-002.md) | BSLib debug | minor | Stack frame text prints the decorated name; the undecorated one is thrown away |
| [BSLIB-003](BSLIB-003.md) | BSLib debug | minor | Call stack in the assert dialog is cut at 520 characters although the buffer holds 4096 |
| [BSLIB-004](BSLIB-004.md) | BSLib debug | minor | Minidump: `CreateFileA` failure is not checked; dump names can collide |
| [BSLIB-005](BSLIB-005.md) | BSLib debug | minor | dbghelp.dll is reloaded on every assert after a failed load; freed handle kept |
| [BSLIB-006](BSLIB-006.md) | BSLib debug | wrong | Assert dialog silently breaks into the debugger when the message is longer than 1023 characters |
| [BSNET-001](BSNET-001.md) | CMsg | wrong | String writes clamp without the 2-byte length prefix and throw instead of truncating |
| [BSNET-002](BSNET-002.md) | CMsg | crash | Reading past the end of a received packet crashes the process on purpose |
| [BSNET-003](BSNET-003.md) | CMsg | wrong | String reads in pop-back mode return without reading and leave the output unchanged |
| [BSNET-004](BSNET-004.md) | CMsg | minor | Constructor copies an uninitialised pointer into the WSABUF (overwritten right after) |
| [DB-001](DB-001.md) | Database | wrong | Pool connections are freed without `SQLDisconnect`; connections leak |
| [DB-002](DB-002.md) | Database | crash | `Execute` frees an uninitialised statement handle when no connection is free |
| [DB-003](DB-003.md) | Database | crash | SQL diagnostics and query text are used as printf formats |
| [DB-004](DB-004.md) | Database | minor | SQLSTATE and message compared uninitialised when there is no diagnostic record |
| [DB-005](DB-005.md) | Database | wrong | Delete by key executes only the WHERE clause |
| [DB-006](DB-006.md) | Database | wrong | An unknown parameter type returns success without executing |
| [DB-007](DB-007.md) | Database | crash | The `SQLMoreResults` loop never ends on an error |
| [DB-008](DB-008.md) | Database | minor | `DescribeColumns` leaks the statement for a table without columns |
| [DB-009](DB-009.md) | Database | minor | GetData fetch leaks the record on a failed `SQLGetData` |
| [DB-010](DB-010.md) | Database | crash | Columns without a format text crash the describe step (`strlen(NULL)`) |
| [DB-011](DB-011.md) | Database | minor | Trailing blanks trimmed past the end of a truncated value |
| [DB-012](DB-012.md) | Database | crash | Bound fetch loops never end on a fetch error |
| [DL-001](DL-001.md) | DownloadServer | minor | An empty patch relay names its directory from uninitialised bytes |
| [DL-002](DL-002.md) | DownloadServer | wrong | `USER_LIMIT` counts every engine session, server connections included |
| [FM-001](FM-001.md) | FarmManager (2007 framework) | wrong | A refused second cord session is still treated as established; patch data dropped and handshake replies sent to the closed session |
| [FM-002](FM-002.md) | FarmManager (2007 framework) | wrong | Without a cord, the 0x6005 query lists every body the server cannot reach |
| [FILE-001](FILE-001.md) | Files | wrong | `CFileMem::Open` allocates a new buffer on every open; the old one leaks |
| [FILE-002](FILE-002.md) | Files | wrong | `CFileMM` opens for reading with `OPEN_ALWAYS`: missing files are created empty; error paths leak handles |
| [FILE-003](FILE-003.md) | Files | minor | `IFile::Read` logs the 4090-byte block limit instead of the requested size |
| [FILE-004](FILE-004.md) | Files | minor | Lower-casing reads past a string that ends with a DBCS lead byte |
| [FILE-005](FILE-005.md) | Files | wrong | Cache purge subtracts files that were never counted, so cache usage is under-reported |
| [FWAPP-001](FWAPP-001.md) | Server app | wrong | Message handler table is one entry short; the last handler overwrites the timer handle |
| [FWAPP-002](FWAPP-002.md) | Server app | minor | Crash-dump error file name misses the `\` after the module directory |
| [FWAPP-003](FWAPP-003.md) | Server app | wrong | Private-address test compares bit subsets instead of prefixes |
| [FWAPP-004](FWAPP-004.md) | Server app | crash | Overlap job pools and the ID pool are used from several threads without a lock |
| [FWAPP-005](FWAPP-005.md) | Server app | crash | A lost session walks the overlap job map without its lock |
| [FWAPP-006](FWAPP-006.md) | Server app | minor | A 0x6009 message leaks when the target queue refuses it |
| [FWAPP-007](FWAPP-007.md) | Server app | minor | A massive relay leaves its job registered when the copy fails |
| [FWAPP-008](FWAPP-008.md) | Server app | crash | The lost-session log indexes the error name table without bounds |
| [FWAPP-009](FWAPP-009.md) | Server app | crash | The body-state observer keeps pointing to the deleted trigger |
| [FWAPP-010](FWAPP-010.md) | Server app | crash | The frame window pointer dangles when the window cannot be created |
| [FWAPP-011](FWAPP-011.md) | Server app | wrong | The patch-file directory is formatted but never appended |
| [FWAPP-012](FWAPP-012.md) | Server app | crash | The file-transfer error log passes a `std::string` object for `%s` |
| [FWAPP-013](FWAPP-013.md) | Server app | crash | The service name is copied with a 256-character limit into 64 bytes |
| [FWAPP-014](FWAPP-014.md) | Server app | minor | A second instance writes its startup error file to the drive root |
| [FWAPP-015](FWAPP-015.md) | Server app | wrong | The quit request never reaches the console and service loops |
| [FWAPP-016](FWAPP-016.md) | Server app | minor | Service uninstall leaks handles and always reports failure |
| [FWDATA-001](FWDATA-001.md) | Server data | minor | The second route map is freed only when it is already empty |
| [FWDATA-002](FWDATA-002.md) | Server data | minor | Status broadcasts go to session 0 for notify bodies that are not connected |
| [FWDATA-003](FWDATA-003.md) | Server data | minor | A duplicate privileged-IP group leaks its address set |
| [FWUI-001](FWUI-001.md) | Monitor GUI | none | Withdrawn: the popup menus do pass `TPM_RETURNCMD` (0x100); not a bug |
| [FWUI-002](FWUI-002.md) | Monitor GUI | crash | The monitor view is deleted while its window still exists (use after free at shutdown) |
| [FWUI-003](FWUI-003.md) | Monitor GUI | crash | Graph names copied with a 256-character limit into a 64-byte field |
| [FWUI-004](FWUI-004.md) | Monitor GUI | minor | Architecture view icons are freed with `DeleteObject`; overlay handle not cleared |
| [FWUI-005](FWUI-005.md) | Monitor GUI | crash | Cord status and body state index tables without a range check |
| [FWUI-006](FWUI-006.md) | Monitor GUI | wrong | Rebuilding the architecture view keeps old entries and duplicates children |
| [FWUI-007](FWUI-007.md) | Monitor GUI | minor | Colour codes below '0' read before the colour table |
| [FWUI-008](FWUI-008.md) | Monitor GUI | minor | The PERFMON log line is formatted into itself and starts with ", " |
| [FWUI-009](FWUI-009.md) | Monitor GUI | crash | Counters of type 0 have no type name; the legend calls strlen(NULL) |
| [FWUI-010](FWUI-010.md) | Monitor GUI | crash | Queued dialog texts are freed but stay in the list (double free) |
| [FWUTIL-001](FWUTIL-001.md) | Server config | crash | Server.cfg certification addresses copied with a 256-character limit into 16-byte fields |
| [FWUTIL-002](FWUTIL-002.md) | Server config | crash | Server.cfg tokenizer copies tokens into a 1 KB stack buffer without a size limit |
| [FWUTIL-003](FWUTIL-003.md) | Server config | minor | The Server.cfg text buffer is never freed |
| [FWUTIL-004](FWUTIL-004.md) | Monitor | wrong | Net engine counters are registered with a sample limit of 0, so their graphs stay empty |
| [FWUTIL-005](FWUTIL-005.md) | Monitor | minor | The monitor thread handle is never closed; a stale stop event is kept |
| [FWUTIL-006](FWUTIL-006.md) | Date/time | crash | `CDateTime` getters dereference NULL when `localtime` rejects the time |
| [FWUTIL-007](FWUTIL-007.md) | Date/time | wrong | Local smalldatetime ignores daylight saving time |
| [GW-001](GW-001.md) | Gateway | crash | Unknown shard ID in 0x6105 writes through a NULL pointer (any handshaked client) |
| [GW-002](GW-002.md) | Gateway | wrong | One shard without an AgentServer ends the shard map build |
| [GW-003](GW-003.md) | Gateway | crash | Failed `_PrivilegedIP` load frees the module version lists twice |
| [GW-004](GW-004.md) | Gateway | security | Account database connection string, password included, written to the log |
| [GW-005](GW-005.md) | Gateway | security | Quote filter of the login strings tests the wrong byte; quotes reach the certification SQL |
| [GW-006](GW-006.md) | Gateway | minor | Notice count of 0xA104 wraps at 256 |
| [GWCERT-001](GWCERT-001.md) | Gateway cert | crash | Handler table entries for divisions 0 and 27 are invalid; logins there crash the certifier thread |
| [GWCERT-002](GWCERT-002.md) | Gateway cert | wrong | Certification records leak on failed logins and failed relays |
| [GWCERT-003](GWCERT-003.md) | Gateway cert | security | Captcha codes come from a never-seeded `rand()`; the sequence repeats after every start |
| [GWCERT-004](GWCERT-004.md) | Gateway cert | minor | The reserve captcha images of the start-up are never made |
| [GWCERT-005](GWCERT-005.md) | Gateway cert | security | Yahoo certification logs passwords in clear text |
| [GWCERT-006](GWCERT-006.md) | Gateway cert | crash | Yahoo login check reads up to 1024 bytes into a 256-byte buffer |
| [GWCERT-007](GWCERT-007.md) | Gateway cert | hang | A privileged address range ending at 255.255.255.255 loops forever |
| [GWCERT-008](GWCERT-008.md) | Gateway cert | wrong | Yahoo age check takes its last field from the wrong string |
| [GWCERT-009](GWCERT-009.md) | Gateway cert | wrong | Joymax login-fail notice repeats the login request when parsing fails |
| [GWCERT-010](GWCERT-010.md) | Gateway cert | minor | Captcha image record lost when drawing fails |
| [GWCERT-011](GWCERT-011.md) | Gateway cert | wrong | Captcha failure counts per address never reset |
| [GWCERT-012](GWCERT-012.md) | Gateway cert | minor | Handlers read the result row when the query returned none |
| [GWCERT-013](GWCERT-013.md) | Gateway cert | security | `_CertifyUser_SU` puts the unfiltered password into the SQL text (SQL injection) |
| [MM-001](MM-001.md) | MachineManager | wrong | `PdhCloseQuery` gets the address of the query handle; the query is never closed |
| [MM-002](MM-002.md) | MachineManager | minor | 0x6301 never closes the process and thread handles of a started module |
| [MM-003](MM-003.md) | MachineManager | wrong | WM_CLOSE to a windowless module goes to MachineManager's own queue; the module is always killed after the timeout |
| [MM-004](MM-004.md) | MachineManager | wrong | Windowless modules are found by exe-name prefix; a stop can terminate an unrelated program |
| [NET-001](NET-001.md) | NetEngine | minor | `Initialize` starts Winsock with the previous parameter block's version |
| [NET-002](NET-002.md) | NetEngine | minor | `GetStatistics` reports the accepted-socket pool for the connect pool too |
| [NET-003](NET-003.md) | NetEngine | wrong | `Connect` and `AddKeepAliveTarget` resolve the bind address but never use it |
| [NET-004](NET-004.md) | NetEngine | minor | MAC address lookup allocates 640 times the needed buffer |
| [NET-005](NET-005.md) | NetEngine | crash | Logging crashes when there is no engine (before construction or after `Shutdown`) |
| [NET-006](NET-006.md) | NetEngine | minor | A session event that cannot be queued is never released |
| [NET-007](NET-007.md) | NetEngine | wrong | A session whose socket cannot be attached stays registered after its connect event |
| [NET-008](NET-008.md) | NetEngine | wrong | A file transfer offset only moves the byte counter, not the file position |
| [NET-009](NET-009.md) | NetEngine | minor | `CreateNewSession` leaks the session when no ID is left |
| [NET-010](NET-010.md) | NetEngine | crash | `DumpMsgPool` walks the in-use set without the pool lock |
| [NET-011](NET-011.md) | NetEngine | wrong | Accepted sessions are announced before the peer address is stored |
| [NET-012](NET-012.md) | NetEngine | minor | Keep-alive reconnects block with the lock held, stalling session closes |
| [NET-013](NET-013.md) | NetEngine | minor | A second disconnect overwrites the close reason and posts another `TransmitFile` |
| [PROACT-001](PROACT-001.md) | Proactor | crash | A failed completion of a pooled message is released twice |
| [PROACT-002](PROACT-002.md) | Proactor | crash | Event proactor leaves NULL handles in its wait array; the worker exits |
| [PROACT-003](PROACT-003.md) | Proactor | minor | Failed stop packets are never detected |
| [PROACT-004](PROACT-004.md) | Proactor | crash | `Proactor_Create` initializes the created job without a NULL check |
| [PROACT-005](PROACT-005.md) | Proactor | wrong | Stopping the overlapped peer process does not drain its send queue; messages leak |
| [SEC-001](SEC-001.md) | Security | wrong | Blowfish encryption does not pad buffers shorter than 7 bytes and reads 8 source bytes |
| [SEC-002](SEC-002.md) | Security | crash | Random prime search can divide by zero or return 1 for small start values |
| [THREAD-001](THREAD-001.md) | Threads | crash | Thread start thunk runs from heap memory; faults when DEP is enforced |
| [THREAD-002](THREAD-002.md) | Threads | crash | `CThreadControl::Stop` uses the job pointer without a NULL check |
| [THREAD-003](THREAD-003.md) | Threads | crash | Random ideal-processor choice can divide by zero or pick invalid CPUs |
| [THREAD-004](THREAD-004.md) | Threads | minor | Stopped thread controls stay in the manager's map until shutdown |
| [THREAD-005](THREAD-005.md) | Threads | crash | Task destructor clears its thread manager first, so worker threads are never stopped |
| [THREAD-006](THREAD-006.md) | Threads | crash | Task queue stopped without a NULL check |
| [SOCK-001](SOCK-001.md) | Sockets | crash | mswsock.dll is freed after `AcceptEx`/`TransmitFile` pointers are taken from it |
| [SOCK-002](SOCK-002.md) | Sockets | wrong | Socket buffer size helper halves the size after every success |
| [SOCK-003](SOCK-003.md) | Sockets | wrong | A message rejected by the send queue limit is never released |
| [SOCK-004](SOCK-004.md) | Sockets | minor | A failed `TransmitFile` disconnect is never detected |
| [SOCK-005](SOCK-005.md) | Sockets | crash | A datagram without a message task is released twice (unreachable today) |
| [SOCK-006](SOCK-006.md) | Sockets | minor | UDP ping packets carry four uninitialised stack bytes |
| [SOCK-007](SOCK-007.md) | Sockets | crash | A UDP ping host name that does not resolve crashes the process |
| [SOCK-008](SOCK-008.md) | Sockets | minor | UDP ping receive errors are only reported when formatting fails |
| [SOCK-009](SOCK-009.md) | Sockets | wrong | Removing a UDP ping target is inverted |
| [SOCK-010](SOCK-010.md) | Sockets | wrong | The handshake state byte is never initialised |
| [SRVFW-001](SRVFW-001.md) | ServerFramework | crash | Crash-dump fallback writes its .txt only when `fopen_s` fails, through an invalid `FILE*` |
| [SRVFW-002](SRVFW-002.md) | CMassiveMsg | wrong | Reads and writes cross at most one part boundary and throw for anything larger |
| [SRVFW-003](SRVFW-003.md) | CMassiveMsg | minor | Peeking the inner opcode leaves the read cursor one byte further on two paths |
| [AS-001](AS-001.md) | AgentServer | crash | A refused complete massive message is freed twice (Dispatch and OnMsg_600D) |
| [AS-002](AS-002.md) | AgentServer | wrong | A session ID 0 stops the 0x220A multi-session relay |
| [AS-003](AS-003.md) | AgentServer | minor | 0x2209 counts the bytes of an already released message |
| [AS-004](AS-004.md) | AgentServer | crash | XTrap_S_SetActiveCode is called without being checked after the DLL load |
| [AS-005](AS-005.md) | AgentServer | minor | 0x6103 compares the token with an unset address when the peer address is unknown |
| [AS-006](AS-006.md) | AgentServer | crash | A forwarded client packet is released twice |
| [SM-001](SM-001.md) | SR_ShardManager DB records | crash | A full stream record buffer ends the process with an empty rethrow |
| [SM-002](SM-002.md) | SR_ShardManager DB records | minor | A blank CHAR column is stored as one blank instead of an empty string |
| [SM-003](SM-003.md) | SR_ShardManager DB records | minor | COS owner changes are never marked for the database update |
| [SM-004](SM-004.md) | SR_ShardManager DB records | minor | Resetting a server event reward does not mark RewardID for the update |
| [SM-005](SM-005.md) | SR_ShardManager DB records | crash | An item with more than 12 magic parameters overruns the parameter array |
| [SM-006](SM-006.md) | SR_ShardManager reference data | crash | Reading past the end of a reference data row |
| [SM-007](SM-007.md) | SR_ShardManager reference data | minor | A reference row with a duplicate key leaks |
| [SM-008](SM-008.md) | SR_ShardManager reference data | minor | A reference row out of service leaks |
| [SM-009](SM-009.md) | SR_ShardManager reference data | minor | Siege rows out of service are loaded |
| [SM-010](SM-010.md) | SR_ShardManager reference data | minor | Every _RefGameWorldNPC row leaks |
| [SM-011](SM-011.md) | SR_ShardManager reference data | major | A _RefRentItem row out of service stops the build |
| [SM-012](SM-012.md) | SR_ShardManager reference data | minor | A _RefMagicOptAssign Race of 4 or more writes past the assign tables |
| [SM-013](SM-013.md) | SR_ShardManager reference data | minor | The "can't find MagicOptGroup" message passes a std::string to %s |
| [SM-020](SM-020.md) | SR_ShardManager reference data | minor | The silk price check of the package items never fails |
| [SM-021](SM-021.md) | SR_ShardManager reference data | minor | Every _RefPricePolicyOfItem row leaks |
| [SM-022](SM-022.md) | SR_ShardManager reference data | minor | The shop goods are never freed |
| [SM-030](SM-030.md) | SR_ShardManager reference data | minor | A rejected _RefDropItemGroup row is kept in group 0 |
| [SM-031](SM-031.md) | SR_ShardManager reference data | major | A _RefMonster_AssignedItemRndDrop row out of service fails the reference build |
| [SM-032](SM-032.md) | SR_ShardManager reference data | minor | A drop item of a country above 3 is added past the drop class tables |
| [SM-033](SM-033.md) | SR_ShardManager reference data | wrong | A customizing drop of group 16 is written over the monster's Except columns |
| [SM-040](SM-040.md) | SR_ShardManager reference data | minor | A _RefObjCommon row with a duplicate ID leaks its object |
| [SM-041](SM-041.md) | SR_ShardManager reference data | major | An object item row that is missing crashes the reader |
| [SM-042](SM-042.md) | SR_ShardManager reference data | minor | A structure is added to its region twice |
| [SM-050](SM-050.md) | SR_ShardManager reference data | major | A teleport link with an unknown owner teleport crashes the start-up |
| [SM-051](SM-051.md) | SR_ShardManager reference data | minor | A teleport link out of service leaks |
| [SM-052](SM-052.md) | SR_ShardManager reference data | minor | A fortress area without its region is reported as an unknown fortress |
| [SM-060](SM-060.md) | SR_ShardManager reference data | minor | A nest of an unknown hive crashes the reference data build |
| [SM-061](SM-061.md) | SR_ShardManager reference data | minor | The ranking table rows are never freed |
| [SM-070](SM-070.md) | SR_ShardManager reference data | minor | The "already has this index" line of the collection book has three %s and two arguments |
| [SM-071](SM-071.md) | SR_ShardManager reference data | major | A gacha free parameter with '<' but no '>' never finishes parsing |
| [SM-072](SM-072.md) | SR_ShardManager reference data | minor | A gacha item set row with an unknown object crashes the reader |
| [SM-100](SM-100.md) | SR_ShardManager climate | minor | A second climate row with the same ID leaks its CClimate |
| [SM-101](SM-101.md) | SR_ShardManager climate | minor | Rain and snow amounts wrap instead of being held at 255 |
| [SM-102](SM-102.md) | SR_ShardManager game worlds | minor | A reused game world layer keeps the old create parameter list |
| [SM-103](SM-103.md) | SR_ShardManager game worlds | minor | Instance worlds with WorldMaxCount 0 cannot open a layer |
| [SM-104](SM-104.md) | SR_ShardManager game worlds | minor | "Already Closed World" logs the packed world and layer as the world ID |
| [SM-110](SM-110.md) | SR_ShardManager association reputation | minor | The association records are never given back |
| [SM-111](SM-111.md) | SR_ShardManager association reputation | minor | Unknown temple codes count as Anubis |
| [SM-112](SM-112.md) | SR_ShardManager association reputation | minor | A Haroeris draw has no winner |
| [SM-113](SM-113.md) | SR_ShardManager log jobs | minor | The log functions keep the job on their early failures |
| [SM-114](SM-114.md) | SR_ShardManager guild siege authority | minor | The missing member check of the siege authority never fires |
| [SM-131](SM-131.md) | SR_ShardManager schedules | minor | A service-start event without a group crashes before the NULL check |
| [SM-132](SM-132.md) | SR_ShardManager schedules | minor | DeleteInstance destroys the running job copy as IScheduleJob |
| [SM-133](SM-133.md) | SR_ShardManager schedules | minor | The "WorkingStateWhenServiceStart is not exist" log passes the string object for %s |
| [SM-134](SM-134.md) | SR_ShardManager temple gates | cosmetic | The Anubis and Isis temple opening is logged as "Anubis And Neith" |
| [SM-135](SM-135.md) | SR_ShardManager server events | minor | Completion and reward message leaks on the error returns |
| [SM-136](SM-136.md) | SR_ShardManager server events | cosmetic | The remaining reward time update logs a failed delete |
| [SM-137](SM-137.md) | SR_ShardManager server events | minor | The delayed job heap is ordered by the delay, not by the due time |
| [SM-138](SM-138.md) | SR_ShardManager server events | major | A failed load frees records that loaded events already hold |
| [SM-139](SM-139.md) | SR_ShardManager server events | major | A recycled CServerEvent cannot be attached again |
| [SM-140](SM-140.md) | SR_ShardManager server events | minor | The record is used after it went back to its table |
| [SM-141](SM-141.md) | SR_ShardManager server events | minor | A missing event is handed to the end log as NULL |
| [SM-142](SM-142.md) | SR_ShardManager server events | minor | DeleteServerEvent loses the event on its late failures |
| [SM-143](SM-143.md) | SR_ShardManager server events | minor | AllocNewServerEventReward leaks on its failures |
| [SM-144](SM-144.md) | SR_ShardManager server events | minor | SyncData leaves the stream writer bound on its early returns |
| [SM-145](SM-145.md) | SR_ShardManager schedules | minor | The running copy of a schedule job is never freed |
| [SM-160](SM-160.md) | SR_ShardManager siege fortress | minor | Siege fortress relay broadcasts the received message and leaks the relay |
| [SM-161](SM-161.md) | SR_ShardManager flag world | minor | Flag world level request keeps the JID of a participant who went offline |
| [SM-162](SM-162.md) | SR_ShardManager battle arena | major | Battle arena check answer is not range checked |
| [SM-163](SM-163.md) | SR_ShardManager battle arena | minor | Party arena layer opened before the party members are looked up |
| [SM-164](SM-164.md) | SR_ShardManager battle arena | minor | Random arena team keeps the level of the removed member |
| [SM-165](SM-165.md) | SR_ShardManager battle arena | minor | Party and guild level lists are not cleared before the team building |
| [SM-166](SM-166.md) | SR_ShardManager battle arena | crash | Job arena team without members divides by zero |
| [SM-200](SM-200.md) | SR_ShardManager guilds | minor | A guild level-up whose master is offline is answered as a failure |
| [SM-201](SM-201.md) | SR_ShardManager guilds | minor | The failed hostile-guild withdrawal is logged with a missing argument |
| [SM-202](SM-202.md) | SR_ShardManager guilds | minor | The GP history clean-up result of a guild load is never checked |
| [SM-203](SM-203.md) | SR_ShardManager guilds | major | A guild war start that fails is answered as success or not at all |
| [SM-240](SM-240.md) | SR_ShardManager messenger | minor | `_Friend_Remove` error codes are never recognised |
| [SM-241](SM-241.md) | SR_ShardManager party matching | minor | The master's party matching notice names the requester |
| [SM-242](SM-242.md) | SR_ShardManager party matching | minor | A failed accepted party matching join keeps the entry locked |
| [SM-243](SM-243.md) | SR_ShardManager party matching | major | Party matching answer crashes when both users are gone |
| [SM-270](SM-270.md) | SR_ShardManager party matching | minor | Party matching requests trust the user's party ID |
| [SM-271](SM-271.md) | SR_ShardManager party matching | minor | A party matching entry that fails to write leaves bytes in the list page |
| [SM-272](SM-272.md) | SR_ShardManager party matching | minor | Party matching list rebuild leaks a page message |
| [SM-273](SM-273.md) | SR_ShardManager training camps | minor | A failed accepted mentor application keeps the recruit entry locked |
| [SM-274](SM-274.md) | SR_ShardManager training camps | minor | Recruit list rebuild leaks a page message |
| [SM-275](SM-275.md) | SR_ShardManager training camps | minor | Training camp honor points are cut to one byte |
| [SM-276](SM-276.md) | SR_ShardManager training camps | minor | A failed camp disband removes the members from the character map |
| [SM-277](SM-277.md) | SR_ShardManager votes | major | A reused vote keeps the tally of the previous vote |
| [SM-300](SM-300.md) | SR_ShardManager admin queries | minor | The association reputation statement is used as a format string |
| [SM-301](SM-301.md) | SR_ShardManager admin queries | major | A character data request with an unknown mode calls through a NULL editor |
| [SM-302](SM-302.md) | SR_ShardManager shard maintainer | minor | A failed revive statement is reported as a restored character |
| [SM-303](SM-303.md) | SR_ShardManager shard maintainer | major | Reviving a character can drop another character from the delete schedule |
| [SM-304](SM-304.md) | SR_ShardManager one-by-one processor | major | The job timeout check steps from an erased map node |
| [SM-305](SM-305.md) | SR_ShardManager one-by-one processor | minor | Silk history queries leak their message in the group 3 divisions |
| [SM-306](SM-306.md) | SR_ShardManager one-by-one processor | minor | A repeated silk history key leaks its request data |
| [SM-307](SM-307.md) | SR_ShardManager admin queries | minor | The statement jobs use their statement as a format string |
| [SM-308](SM-308.md) | SR_ShardManager admin queries | major | Statement jobs put console strings into SQL unchecked |
| [SM-309](SM-309.md) | SR_ShardManager admin queries | minor | Record lists freed through the wrong table |
| [SM-310](SM-310.md) | SR_ShardManager admin queries | minor | The character information query leaks its records |
| [SM-311](SM-311.md) | SR_ShardManager admin queries | minor | Skill page looks reference skills up with lower_bound |
| [SM-312](SM-312.md) | SR_ShardManager admin queries | minor | The unknown server event type log misses an argument |
| [SM-313](SM-313.md) | SR_ShardManager admin queries | minor | The log query jobs leak the selected records |
| [SM-314](SM-314.md) | SR_ShardManager admin queries | major | Standard shard schedules copy their parameter text with a 256-byte limit |
| [SM-315](SM-315.md) | SR_ShardManager admin queries | minor | Log job descriptions grow with every reuse of the job object |
| [SM-316](SM-316.md) | SR_ShardManager admin queries | minor | The siege data answer counts requests it does not write |
| [SM-317](SM-317.md) | SR_ShardManager admin queries | minor | The rent information query leaks its records |
| [SM-318](SM-318.md) | SR_ShardManager admin queries | minor | A cut SMC log statement of the character data editor is still executed |
| [SM-340](SM-340.md) | SR_ShardManager admin queries | major | A timed job edit with an unknown operation runs an uninitialised statement |
| [SM-341](SM-341.md) | SR_ShardManager admin queries | major | Item edits put unchecked names from the item row into the statement |
| [SM-350](SM-350.md) | SR_ShardManager admin queries | minor | Some magic options replace the item description built so far |
| [SM-351](SM-351.md) | SR_ShardManager admin queries | minor | The guild and training camp queries leak their records |
| [SM-352](SM-352.md) | SR_ShardManager admin queries | minor | The pet query leaks records when an item query fails |
| [SM-353](SM-353.md) | SR_ShardManager admin queries | minor | LoadPetItem frees the pet list through a NULL table |
