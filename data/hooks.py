#!/usr/bin/python2.4

from optparse import OptionParser
import user, string, dircache, sys, os.path

    
class HookFiles(object):
    """ represents all hook files """

    # the hooks are stored here
    hookDir = "/var/lib/update-notifier/user.d/"
    
    class HookFile(object):
        """ represents a single hook file """
        __slots__ = [ "filename", "mtime", "cmd_run", "seen" ]
        
        def __init__(self, filename):
            self.filename = filename
            self.mtime = os.stat(HookFiles.hookDir+filename).st_mtime
            self.cmd_run = False
            self.seen = False

        def summary(self):
            """ show the summary for the notification """
            # FIXME: read rfc822 style file and get the i18n version of
            # "Name"
	    pass
        summary = property(summary)

        def description(self):
            """ show a long description for the notification """
            # FIXME: read rfc822 style file and get the i18n version of
            # "Description", convert "\n" -> " " and strstrip it afterwards
	    pass
        description = property(description)


    def __init__(self):
        self._hooks = {}
        self._readSeenFile()
        self._update()

    def _readSeenFile(self):
        """ read the users config file that stores what hook files are
            already seen
        """
        data_home = os.getenv("XDG_CONFIG_HOME", os.path.join(user.home, '.config'))
        hooks_seen = os.path.join(data_home, 'update-notifier', 'hooks_seen')
        if os.path.exists(hooks_seen):
            for line in open(hooks_seen):
                filename, mtime, cmd_run = string.split(line)
                if os.path.exists(self.hookDir+filename) and \
                   filename not in self._hooks:
                    h = self.HookFile(filename)
                    h.mtime = mtime
                    h.cmd_run = cmd_run
                    h.seen = True
                    # check if file was motified since we last saw it
                    if os.stat(self.hookDir+filename).st_mtime > int(mtime):
                        h.seen = False
                    self._hooks[filename] = h

    def _update(self):
        """ update hook dict with the current state on the fs """
        for hook in dircache.listdir(self.hookDir):
            if hook in self._hooks:
                # we have it already, check if it was motified since
                # we last saw it
                h = self._hooks[hook]
                if os.stat(self.hookDir+hook).st_mtime > int(h.mtime):
                        h.seen = False
            else:
                self._hooks[hook] = self.HookFile(hook)

    def checkNew(self):
        """ return the list of unseen hook files """
        new = []
        self._update()
        for hook in self._hooks:
            if not self._hooks[hook].seen:
                new.append(self._hooks[hook])
        return new
    

def check():
    hooks = HookFiles()
    new = hooks.checkNew()
    return len(new)

def test():
    hooks = HookFiles()
    new = hooks.checkNew()
    print "Hooks: %s" % len(new)
    for hook in hooks._hooks:
        print hooks._hooks[hook].notification


if __name__ == "__main__":
    parser = OptionParser()
    parser.add_option("-c", "--check", action="store_true", dest="check",
                      default=False, help="check for new hooks")
    parser.add_option("-r", "--run", action="store_true", dest="run",
                      default=False, help="run interactive hook view")
    parser.add_option("-t", "--test", action="store_true", dest="test",
                      default=False, help="run test")
    (options, args) = parser.parse_args()


    if options.check:
        sys.exit(check())
    elif options.run:
        run()
    elif options.test:
        test()
