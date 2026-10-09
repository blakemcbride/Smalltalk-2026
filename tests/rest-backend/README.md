The back end the Rest-Server tests and gate serve: a `Login` and a handful of
services, each a Tonel file loaded on the first request for it.  The scratch
service the tests write -- `Scratch<n>.class.st`, under a name no file here
has, chosen per run -- is removed by the tests themselves; a run that was
killed can leave one behind.
