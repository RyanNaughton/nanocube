#!/bin/bash
mkdir -p .aux
file_queries=".aux/name_query"
cat >$file_queries << END
schema               schema()
total                q(crimes)
loc1                 q(crimes.b('location',dive(p(2,1,2),8)))
loc1t                format('text');q(crimes.b('location',dive(p(2,1,2),8)))
loc1i                format('text');q(crimes.b('location',dive(p(2,1,2),8),'img8'))
loc1i2               format('text');q(crimes.b('location',dive(p(2,1,2),8),'img11'))
type1                q(crimes.b('type',dive(1)))
type1n               q(crimes.b('type',dive(1),'name'))
type1nt              format('text');q(crimes.b('type',dive(1),'name'))
type1ntheft          q(crimes.b('type',p(2)))
type1atheft          q(crimes.b('type','THEFT'))
type1ntheftbur       q(crimes.b('type',pathagg(p(2),p(4))))
type1atheftbur       q(crimes.b('type',pathagg('THEFT','BURGLARY')))
time1intseq          q(crimes.b('time',intseq(480,24,10,24)))
time1timeseries      q(crimes.b('time',timeseries('2013-12-21T00:00-06',24*3600,10,24*3600)))
loc1i3               format('text');q(crimes.b('location',dive_list(dive(p(2,1,2,0,0,0,0,1,2),1),dive(p(2,1,2,0,0,0,0,1,3),1))))
poly1                q(crime.b('location',region(18,poly('41.8595,-87.6565,41.8969,-87.6565,41.8969,-87.6013,41.8595,-87.6013'))))

END

prefix="http://localhost:51234"
while IFS='' read -r line || [[ -n "$line" ]]; do
# 	echo "$line"
	name=$(echo "$line" | tr -s ' ' | cut -f 1 -d' ')
	query=$(echo "$line" | tr -s ' ' | cut -f 2 -d' ')
# 	echo "$name"
# 	echo "$query"
	wget -q -O "${name}.result" "${prefix}/${query}"
done < "$file_queries"
