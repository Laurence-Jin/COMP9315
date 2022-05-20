rm  -f  R.*
./create  R  5  2  "0,1:1,1:2,1:3,1:4,1"
./insert R < data0.txt
#./select R '1042,?,?' | sort
#./select R '?,?,?' | sort
#./select R '?,horoscope,?' | sort
./stats  R
#./select R '1001,chair,shoes'