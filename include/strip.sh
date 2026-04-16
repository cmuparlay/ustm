# Strips out all the ifdefs from ustm_swiss.h to generate ustm.h
# cppp code is from : https://www.muppetlabs.com/~breadbox/software/cppp.html
/home/guyb/cppp-2.9/cppp ustm_swiss.h -UFunnel -UFunnelEager -USimpleFunnel -UEager -UHWStamp -USimpleSTM -ULongJmp -UTryThrow -USTMStats -UParlayAlloc -ULazyStamp -UVLA -DSplitStamp > ustm.h
