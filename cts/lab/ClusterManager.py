""" ClusterManager class for Pacemaker's Cluster Test Suite (CTS)
"""

__copyright__ = "Copyright 2000-2021 the Pacemaker project contributors"
__license__ = "GNU General Public License version 2 or later (GPLv2+) WITHOUT ANY WARRANTY"

import os
import re
import time

from collections import UserDict

from cts.CTSvars     import *
from cts.CTS         import NodeStatus
from cts.logging     import LogFactory
from cts.watcher     import LogWatcher
from cts.remote      import RemoteFactory
from cts.environment import EnvFactory
from cts.patterns    import PatternSelector

has_log_stats = {}
log_stats_bin = CTSvars.CRM_DAEMON_DIR + "/cts_log_stats.sh"
log_stats = """
#!%s
# Tool for generating system load reports while CTS runs

trap "" 1

f=$1; shift
action=$1; shift
base=`basename $0`

if [ ! -e $f ]; then
    echo "Time, Load 1, Load 5, Load 15, Test Marker" > $f
fi

function killpid() {
    if [ -e $f.pid ]; then
       kill -9 `cat $f.pid`
       rm -f $f.pid
    fi
}

function status() {
    if [ -e $f.pid ]; then
       kill -0 `cat $f.pid`
       return $?
    else
       return 1
    fi
}

function start() {
    # Is it already running?
    if
        status
    then
        return
    fi

    echo Active as $$
    echo $$ > $f.pid

    while [ 1 = 1 ]; do
        uptime | sed s/up.*:/,/ | tr '\\n' ',' >> $f
        #top -b -c -n1 | grep -e usr/libexec/pacemaker | grep -v -e grep -e python | head -n 1 | sed s@/usr/libexec/pacemaker/@@ | awk '{print " 0, "$9", "$10", "$12}' | tr '\\n' ',' >> $f
        echo 0 >> $f
        sleep 5
    done
}

case $action in
    start)
        start
        ;;
    start-bg|bg)
        # Use c --ssh -- ./stats.sh file start-bg
        nohup $0 $f start >/dev/null 2>&1 </dev/null &
        ;;
    stop)
        killpid
        ;;
    delete)
        killpid
        rm -f $f
        ;;
    mark)
        uptime | sed s/up.*:/,/ | tr '\\n' ',' >> $f
        echo " $*" >> $f
        start
        ;;
    *)
        echo "Unknown action: $action."
        ;;
esac
""" % (CTSvars.BASH_PATH)

class ClusterManager(UserDict):
    '''The Cluster Manager class.
    This is an subclass of the Python dictionary class.
    (this is because it contains lots of {name,value} pairs,
    not because it's behavior is that terribly similar to a
    dictionary in other ways.)

    This is an abstract class which class implements high-level
    operations on the cluster and/or its cluster managers.
    Actual cluster managers classes are subclassed from this type.

    One of the things we do is track the state we think every node should
    be in.
    '''

    def __InitialConditions(self):
        #if os.geteuid() != 0:
        #  raise ValueError("Must Be Root!")
        None

    def _finalConditions(self):
        for key in list(self.keys()):
            if self[key] == None:
                raise ValueError("Improper derivation: self[" + key +   "] must be overridden by subclass.")

    def __init__(self, Environment, randseed=None):
        self.Env = EnvFactory().getInstance()
        self.templates = PatternSelector(self.Env["Name"])
        self.__InitialConditions()
        self.logger = LogFactory()
        self.TestLoggingLevel=0
        self.data = {}
        self.name = self.Env["Name"]

        self.rsh = RemoteFactory().getInstance()
        self.ShouldBeStatus={}
        self.ns = NodeStatus(self.Env)
        self.OurNode = os.uname()[1].lower()
        self.__instance_errorstoignore = []

    def __getitem__(self, key):
        if key == "Name":
            return self.name

        print("FIXME: Getting %s from %s" % (key, repr(self)))
        if key in self.data:
            return self.data[key]

        return self.templates.get_patterns(self.Env["Name"], key)

    def __setitem__(self, key, value):
        print("FIXME: Setting %s=%s on %s" % (key, value, repr(self)))
        self.data[key] = value

    def key_for_node(self, node):
        return node

    def instance_errorstoignore_clear(self):
        '''Allows the test scenario to reset instance errors to ignore on each iteration.'''
        self.__instance_errorstoignore = []

    def instance_errorstoignore(self):
        '''Return list of errors which are 'normal' for a specific test instance'''
        return self.__instance_errorstoignore

    def log(self, args):
        self.logger.log(args)

    def debug(self, args):
        self.logger.debug(args)

    def upcount(self):
        '''How many nodes are up?'''
        count = 0
        for node in self.Env["nodes"]:
          if self.ShouldBeStatus[node] == "up":
            count = count + 1
        return count

    def install_support(self, command="install"):
        for node in self.Env["nodes"]:
            self.rsh(node, CTSvars.CRM_DAEMON_DIR + "/cts-support " + command)

    def prepare_fencing_watcher(self, name):
        # If we don't have quorum now but get it as a result of starting this node,
        # then a bunch of nodes might get fenced
        upnode = None
        if self.HasQuorum(None):
            self.debug("Have quorum")
            return None

        if not self.templates["Pat:Fencing_start"]:
            print("No start pattern")
            return None

        if not self.templates["Pat:Fencing_ok"]:
            print("No ok pattern")
            return None

        stonith = None
        stonithPats = []
        for peer in self.Env["nodes"]:
            if self.ShouldBeStatus[peer] != "up":
                stonithPats.append(self.templates["Pat:Fencing_ok"] % peer)
                stonithPats.append(self.templates["Pat:Fencing_start"] % peer)

        stonith = LogWatcher(self.Env["LogFileName"], stonithPats, "StartupFencing", 0, hosts=self.Env["nodes"], kind=self.Env["LogWatcher"])
        stonith.setwatch()
        return stonith

    def fencing_cleanup(self, node, stonith):
        peer_list = []
        peer_state = {}

        self.debug("Looking for nodes that were fenced as a result of %s starting" % node)

        # If we just started a node, we may now have quorum (and permission to fence)
        if not stonith:
            self.debug("Nothing to do")
            return peer_list

        q = self.HasQuorum(None)
        if not q and len(self.Env["nodes"]) > 2:
            # We didn't gain quorum - we shouldn't have shot anyone
            self.debug("Quorum: %d Len: %d" % (q, len(self.Env["nodes"])))
            return peer_list

        for n in self.Env["nodes"]:
            peer_state[n] = "unknown"

        # Now see if any states need to be updated
        self.debug("looking for: " + repr(stonith.regexes))
        shot = stonith.look(0)
        while shot:
            line = repr(shot)
            self.debug("Found: " + line)
            del stonith.regexes[stonith.whichmatch]

            # Extract node name
            for n in self.Env["nodes"]:
                if re.search(self.templates["Pat:Fencing_ok"] % n, shot):
                    peer = n
                    peer_state[peer] = "complete"
                    self.__instance_errorstoignore.append(self.templates["Pat:Fencing_ok"] % peer)

                elif peer_state[n] != "complete" and re.search(self.templates["Pat:Fencing_start"] % n, shot):
                    # TODO: Correctly detect multiple fencing operations for the same host
                    peer = n
                    peer_state[peer] = "in-progress"
                    self.__instance_errorstoignore.append(self.templates["Pat:Fencing_start"] % peer)

            if not peer:
                self.logger.log("ERROR: Unknown stonith match: %s" % line)

            elif not peer in peer_list:
                self.debug("Found peer: " + peer)
                peer_list.append(peer)

            # Get the next one
            shot = stonith.look(60)

        for peer in peer_list:

            self.debug("   Peer %s was fenced as a result of %s starting: %s" % (peer, node, peer_state[peer]))
            if self.Env["at-boot"]:
                self.ShouldBeStatus[peer] = "up"
            else:
                self.ShouldBeStatus[peer] = "down"

            if peer_state[peer] == "in-progress":
                # Wait for any in-progress operations to complete
                shot = stonith.look(60)
                while len(stonith.regexes) and shot:
                    line = repr(shot)
                    self.debug("Found: " + line)
                    del stonith.regexes[stonith.whichmatch]
                    shot = stonith.look(60)

            # Now make sure the node is alive too
            self.ns.WaitForNodeToComeUp(peer, self.Env["DeadTime"])

            # Poll until it comes up
            if self.Env["at-boot"]:
                if not self.StataCM(peer):
                    time.sleep(self.Env["StartTime"])

                if not self.StataCM(peer):
                    self.logger.log("ERROR: Peer %s failed to restart after being fenced" % peer)
                    return None

        return peer_list

    def StartaCM(self, node, verbose=False):

        '''Start up the cluster manager on a given node'''
        if verbose: self.logger.log("Starting %s on node %s" % (self.templates["Name"], node))
        else: self.debug("Starting %s on node %s" % (self.templates["Name"], node))
        ret = 1

        if not node in self.ShouldBeStatus:
            self.ShouldBeStatus[node] = "down"

        if self.ShouldBeStatus[node] != "down":
            return 1

        patterns = []
        # Technically we should always be able to notice ourselves starting
        patterns.append(self.templates["Pat:Local_started"] % node)
        if self.upcount() == 0:
            patterns.append(self.templates["Pat:DC_started"] % node)
        else:
            patterns.append(self.templates["Pat:NonDC_started"] % node)

        watch = LogWatcher(
            self.Env["LogFileName"], patterns, "StartaCM", self.Env["StartTime"]+10, hosts=self.Env["nodes"], kind=self.Env["LogWatcher"])

        self.install_config(node)

        self.ShouldBeStatus[node] = "any"
        if self.StataCM(node) and self.cluster_stable(self.Env["DeadTime"]):
            self.logger.log ("%s was already started" % (node))
            return 1

        stonith = self.prepare_fencing_watcher(node)
        watch.setwatch()

        if self.rsh(node, self.templates["StartCmd"]) != 0:
            self.logger.log ("Warn: Start command failed on node %s" % (node))
            self.fencing_cleanup(node, stonith)
            return None

        self.ShouldBeStatus[node] = "up"
        watch_result = watch.lookforall()

        if watch.unmatched:
            for regex in watch.unmatched:
                self.logger.log ("Warn: Startup pattern not found: %s" % (regex))

        if watch_result and self.cluster_stable(self.Env["DeadTime"]):
            #self.debug("Found match: "+ repr(watch_result))
            self.fencing_cleanup(node, stonith)
            return 1

        elif self.StataCM(node) and self.cluster_stable(self.Env["DeadTime"]):
            self.fencing_cleanup(node, stonith)
            return 1

        self.logger.log ("Warn: Start failed for node %s" % (node))
        return None

    def StartaCMnoBlock(self, node, verbose=False):

        '''Start up the cluster manager on a given node with none-block mode'''

        if verbose: self.logger.log("Starting %s on node %s" % (self["Name"], node))
        else: self.debug("Starting %s on node %s" % (self["Name"], node))

        self.install_config(node)
        self.rsh(node, self.templates["StartCmd"], synchronous=0)
        self.ShouldBeStatus[node] = "up"
        return 1

    def StopaCM(self, node, verbose=False, force=False):

        '''Stop the cluster manager on a given node'''

        if verbose: self.logger.log("Stopping %s on node %s" % (self["Name"], node))
        else: self.debug("Stopping %s on node %s" % (self["Name"], node))

        if self.ShouldBeStatus[node] != "up" and force == False:
            return 1

        if self.rsh(node, self.templates["StopCmd"]) == 0:
            # Make sure we can continue even if corosync leaks
            # fdata-* is the old name
            #self.rsh(node, "rm -rf /dev/shm/qb-* /dev/shm/fdata-*")
            self.ShouldBeStatus[node] = "down"
            self.cluster_stable(self.Env["DeadTime"])
            return 1
        else:
            self.logger.log ("ERROR: Could not stop %s on node %s" % (self["Name"], node))

        return None

    def StopaCMnoBlock(self, node):

        '''Stop the cluster manager on a given node with none-block mode'''

        self.debug("Stopping %s on node %s" % (self["Name"], node))

        self.rsh(node, self.templates["StopCmd"], synchronous=0)
        self.ShouldBeStatus[node] = "down"
        return 1

    def RereadCM(self, node):

        '''Force the cluster manager on a given node to reread its config
           This may be a no-op on certain cluster managers.
        '''
        rc=self.rsh(node, self.templates["RereadCmd"])
        if rc == 0:
            return 1
        else:
            self.logger.log ("Could not force %s on node %s to reread its config"
            %        (self["Name"], node))
        return None

    def startall(self, nodelist=None, verbose=False, quick=False):

        '''Start the cluster manager on every node in the cluster.
        We can do it on a subset of the cluster if nodelist is not None.
        '''
        map = {}
        if not nodelist:
            nodelist = self.Env["nodes"]

        for node in nodelist:
            if self.ShouldBeStatus[node] == "down":
                self.ns.WaitForAllNodesToComeUp(nodelist, 300)

        if not quick:
            # This is used for "basic sanity checks", so only start one node ...
            if not self.StartaCM(node, verbose=verbose):
                return 0
            return 1

        # Approximation of SimulStartList for --boot
        watchpats = [ ]
        watchpats.append(self.templates["Pat:DC_IDLE"])
        for node in nodelist:
            watchpats.append(self.templates["Pat:InfraUp"] % node)
            watchpats.append(self.templates["Pat:PacemakerUp"] % node)
            watchpats.append(self.templates["Pat:Local_started"] % node)
            watchpats.append(self.templates["Pat:They_up"] % (nodelist[0], node))

        #   Start all the nodes - at about the same time...
        watch = LogWatcher(self.Env["LogFileName"], watchpats, "fast-start", self.Env["DeadTime"]+10, hosts=self.Env["nodes"], kind=self.Env["LogWatcher"])
        watch.setwatch()

        if not self.StartaCM(nodelist[0], verbose=verbose):
            return 0
        for node in nodelist:
            self.StartaCMnoBlock(node, verbose=verbose)

        watch.lookforall()
        if watch.unmatched:
            for regex in watch.unmatched:
                self.logger.log ("Warn: Startup pattern not found: %s" % (regex))

        if not self.cluster_stable():
            self.logger.log("Cluster did not stabilize")
            return 0

        return 1

    def stopall(self, nodelist=None, verbose=False, force=False):

        '''Stop the cluster managers on every node in the cluster.
        We can do it on a subset of the cluster if nodelist is not None.
        '''

        ret = 1
        map = {}
        if not nodelist:
            nodelist = self.Env["nodes"]
        for node in self.Env["nodes"]:
            if self.ShouldBeStatus[node] == "up" or force == True:
                if not self.StopaCM(node, verbose=verbose, force=force):
                    ret = 0
        return ret

    def rereadall(self, nodelist=None):

        '''Force the cluster managers on every node in the cluster
        to reread their config files.  We can do it on a subset of the
        cluster if nodelist is not None.
        '''

        map = {}
        if not nodelist:
            nodelist = self.Env["nodes"]
        for node in self.Env["nodes"]:
            if self.ShouldBeStatus[node] == "up":
                self.RereadCM(node)

    def statall(self, nodelist=None):

        '''Return the status of the cluster managers in the cluster.
        We can do it on a subset of the cluster if nodelist is not None.
        '''

        result = {}
        if not nodelist:
            nodelist = self.Env["nodes"]
        for node in nodelist:
            if self.StataCM(node):
                result[node] = "up"
            else:
                result[node] = "down"
        return result

    def isolate_node(self, target, nodes=None):
        '''isolate the communication between the nodes'''
        if not nodes:
            nodes = self.Env["nodes"]

        for node in nodes:
            if node != target:
                rc = self.rsh(target, self.templates["BreakCommCmd"] % self.key_for_node(node))
                if rc != 0:
                    self.logger.log("Could not break the communication between %s and %s: %d" % (target, node, rc))
                    return None
                else:
                    self.debug("Communication cut between %s and %s" % (target, node))
        return 1

    def unisolate_node(self, target, nodes=None):
        '''fix the communication between the nodes'''
        if not nodes:
            nodes = self.Env["nodes"]

        for node in nodes:
            if node != target:
                restored = 0

                # Limit the amount of time we have asynchronous connectivity for
                # Restore both sides as simultaneously as possible
                self.rsh(target, self.templates["FixCommCmd"] % self.key_for_node(node), synchronous=0)
                self.rsh(node, self.templates["FixCommCmd"] % self.key_for_node(target), synchronous=0)
                self.debug("Communication restored between %s and %s" % (target, node))

    def reducecomm_node(self,node):
        '''reduce the communication between the nodes'''
        rc = self.rsh(node, self.templates["ReduceCommCmd"]%(self.Env["XmitLoss"],self.Env["RecvLoss"]))
        if rc == 0:
            return 1
        else:
            self.logger.log("Could not reduce the communication between the nodes from node: %s" % node)
        return None

    def restorecomm_node(self,node):
        '''restore the saved communication between the nodes'''
        rc = 0
        if float(self.Env["XmitLoss"]) != 0 or float(self.Env["RecvLoss"]) != 0 :
            rc = self.rsh(node, self.templates["RestoreCommCmd"]);
        if rc == 0:
            return 1
        else:
            self.logger.log("Could not restore the communication between the nodes from node: %s" % node)
        return None

    def oprofileStart(self, node=None):
        if not node:
            for n in self.Env["oprofile"]:
                self.oprofileStart(n)

        elif node in self.Env["oprofile"]:
            self.debug("Enabling oprofile on %s" % node)
            self.rsh(node, "opcontrol --init")
            self.rsh(node, "opcontrol --setup --no-vmlinux --separate=lib --callgraph=20 --image=all")
            self.rsh(node, "opcontrol --start")
            self.rsh(node, "opcontrol --reset")

    def oprofileSave(self, test, node=None):
        if not node:
            for n in self.Env["oprofile"]:
                self.oprofileSave(test, n)

        elif node in self.Env["oprofile"]:
            self.rsh(node, "opcontrol --dump")
            self.rsh(node, "opcontrol --save=cts.%d" % test)
            # Read back with: opreport -l session:cts.0 image:<directory>/c*
            if None:
                self.rsh(node, "opcontrol --reset")
            else:
                self.oprofileStop(node)
                self.oprofileStart(node)

    def oprofileStop(self, node=None):
        if not node:
            for n in self.Env["oprofile"]:
                self.oprofileStop(n)

        elif node in self.Env["oprofile"]:
            self.debug("Stopping oprofile on %s" % node)
            self.rsh(node, "opcontrol --reset")
            self.rsh(node, "opcontrol --shutdown 2>&1 > /dev/null")


    def StatsExtract(self):
        if not self.Env["stats"]:
            return

        for host in self.Env["nodes"]:
            log_stats_file = "%s/cts-stats.csv" % CTSvars.CRM_DAEMON_DIR
            if host in has_log_stats:
                self.rsh(host, '''bash %s %s stop''' % (log_stats_bin, log_stats_file))
                (rc, lines) = self.rsh(host, '''cat %s''' % log_stats_file, stdout=2)
                self.rsh(host, '''bash %s %s delete''' % (log_stats_bin, log_stats_file))

                fname = "cts-stats-%d-nodes-%s.csv" % (len(self.Env["nodes"]), host)
                print("Extracted stats: %s" % fname)
                fd = open(fname, "a")
                fd.writelines(lines)
                fd.close()

    def StatsMark(self, testnum):
        '''Mark the test number in the stats log'''

        global has_log_stats
        if not self.Env["stats"]:
            return

        for host in self.Env["nodes"]:
            log_stats_file = "%s/cts-stats.csv" % CTSvars.CRM_DAEMON_DIR
            if not host in has_log_stats:

                global log_stats
                global log_stats_bin
                script=log_stats
                #script = re.sub("\\\\", "\\\\", script)
                script = re.sub('\"', '\\\"', script)
                script = re.sub("'", "\'", script)
                script = re.sub("`", "\`", script)
                script = re.sub("\$", "\\\$", script)

                self.debug("Installing %s on %s" % (log_stats_bin, host))
                self.rsh(host, '''echo "%s" > %s''' % (script, log_stats_bin), silent=True)
                self.rsh(host, '''bash %s %s delete''' % (log_stats_bin, log_stats_file))
                has_log_stats[host] = 1

            # Now mark it
            self.rsh(host, '''bash %s %s mark %s''' % (log_stats_bin, log_stats_file, testnum), synchronous=0)
