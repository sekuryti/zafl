import os

if not os.path.isfile("manifest.txt.config"):
        os.system(os.environ['PEDI_HOME']+'/pedi --setup -m manifest.txt -l zafl -l ps -l zipr -l stratafier -l stars -i ' + os.environ['ZAFL_INSTALL'])


zipr=SConscript("zipr_umbrella/SConstruct")
tools=SConscript("tools/SConstruct")
libzafl=SConscript("libzafl/SConstruct")

Depends(tools,zipr)


ret=[zipr,tools,libzafl]

print "ret=", [str(s) for s in ret]

pedi = Command( target = "./zafl-install",
                source = [zipr,tools,libzafl],
                action = os.environ['PEDI_HOME']+"/pedi -m manifest.txt " )

# decide whether to pedi
if Dir('.').abspath == Dir('#.').abspath:
	ret=ret+pedi


Default(ret)
Return('ret')


