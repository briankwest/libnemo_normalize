/*
 * test_normalize.c -- Pure C test program for libnemo_normalize.
 *
 * Tests text normalization (TN) and inverse text normalization (ITN)
 * across multiple semiotic classes:
 *   TN:  emails, phones, addresses, cardinals, ordinals, money,
 *        dates, times, measures, decimals/fractions, mixed sentences.
 *   ITN: emails, phones, cardinals, ordinals, money, dates, times,
 *        measures, decimals, fractions, mixed sentences.
 *
 * Build:
 *   gcc -o test_normalize test_normalize.c -L. -lnemo_normalize -lstdc++
 *
 * Run:
 *   ./test_normalize
 */

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "nemo_normalize.h"

/* --------------------------------------------------------------------------
 * Timing helpers
 * -------------------------------------------------------------------------- */

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void fmt_time(double seconds, char *buf, int bufsz) {
    if (seconds < 0.001)
        snprintf(buf, bufsz, "%4.0fus", seconds * 1e6);
    else if (seconds < 1.0)
        snprintf(buf, bufsz, "%5.1fms", seconds * 1e3);
    else
        snprintf(buf, bufsz, "%5.2fs", seconds);
}

/* --------------------------------------------------------------------------
 * Test case structure
 * -------------------------------------------------------------------------- */

typedef struct {
    const char *input;
    const char *expected;
} TestCase;

/* --------------------------------------------------------------------------
 * EMAIL TN test cases  (13 cases)
 * -------------------------------------------------------------------------- */

static const TestCase EMAIL_TN_CASES[] = {
    {"cdf@abc.edu", "cdf at abc dot edu"},
    {"abc@gmail.com", "abc at gmail dot com"},
    {"abs@nvidia.com", "abs at NVIDIA dot com"},
    {"asdf123@abc.com", "asdf one two three at abc dot com"},
    {"a1b2@abc.com", "a one b two at abc dot com"},
    {"ab3.sdd.3@gmail.com", "ab three dot sdd dot three at gmail dot com"},
    {"enterprise-services@nvidia.com", "enterprise dash services at NVIDIA dot com"},
    {"nvidia.com", "nvidia dot com"},
    {"test.com", "test dot com"},
    {"http://www.ourdailynews.com.sm", "HTTP colon slash slash WWW dot ourdailynews dot com dot SM"},
    {"https://www.ourdailynews.com.sm", "HTTPS colon slash slash WWW dot ourdailynews dot com dot SM"},
    {"www.ourdailynews.com/123-sm", "WWW dot ourdailynews dot com slash one two three dash SM"},
    {"email is abc1@gmail.com", "email is abc one at gmail dot com"},
};
static const int NUM_EMAIL_TN = sizeof(EMAIL_TN_CASES) / sizeof(EMAIL_TN_CASES[0]);

/* --------------------------------------------------------------------------
 * PHONE TN test cases  (12 cases)
 * -------------------------------------------------------------------------- */

static const TestCase PHONE_TN_CASES[] = {
    {"+1 123-123-5678", "plus one, one two three, one two three, five six seven eight"},
    {"123-123-5678", "one two three, one two three, five six seven eight"},
    {"+1-123-123-5678", "plus one, one two three, one two three, five six seven eight"},
    {"+1 (123)-123-5678", "plus one, one two three, one two three, five six seven eight"},
    {"(123) 123-5678", "one two three, one two three, five six seven eight"},
    {"123-123-5678-1111", "one two three, one two three, five six seven eight, one one one one"},
    {"1-800-GO-U-HAUL", "one, eight hundred, GO U HAUL"},
    {"123.123.0.40", "one two three dot one two three dot zero dot four zero"},
    {"111-11-1111", "one one one, one one, one one one one"},
    {"call me at +1 123-123-5678", "call me at plus one, one two three, one two three, five six seven eight"},
    {"555.555.5555", "five five five, five five five, five five five five"},
    {"(555)555-5555", "five five five, five five five, five five five five"},
};
static const int NUM_PHONE_TN = sizeof(PHONE_TN_CASES) / sizeof(PHONE_TN_CASES[0]);

/* --------------------------------------------------------------------------
 * ADDRESS TN test cases  (8 cases)
 * -------------------------------------------------------------------------- */

static const TestCase ADDRESS_TN_CASES[] = {
    {"2788 San Tomas Expy, Santa Clara, CA 95051",
     "twenty seven eighty eight San Tomas Expressway, Santa Clara, California nine five zero five one"},
    {"2 San Tomas hwy, Santa, FL, 95051",
     "two San Tomas Highway, Santa, Florida, nine five zero five one"},
    {"123 Smth St, City, NY",
     "one twenty three Smth Street, City, New York"},
    {"123 Laguna Ct, Town",
     "one twenty three Laguna Court, Town"},
    {"1211 E Arques Ave",
     "twelve eleven East Arques Avenue"},
    {"708 N 1st St, San City",
     "seven zero eight North first Street, San City"},
    {"12 S 1st st",
     "twelve South first Street"},
    {"Nancy lived at 1428 Elm St. It was a strange place.",
     "Nancy lived at fourteen twenty eight Elm Street. It was a strange place."},
};
static const int NUM_ADDRESS_TN = sizeof(ADDRESS_TN_CASES) / sizeof(ADDRESS_TN_CASES[0]);

/* --------------------------------------------------------------------------
 * CARDINAL TN test cases  (19 cases)
 * -------------------------------------------------------------------------- */

static const TestCase CARDINAL_TN_CASES[] = {
    {"1", "one"},
    {"12", "twelve"},
    {"123", "one hundred and twenty three"},
    {"1234", "twelve thirty four"},
    {"100", "one hundred"},
    {"1000", "one thousand"},
    {"21", "twenty one"},
    {"99", "ninety nine"},
    {"101", "one hundred and one"},
    {"999", "nine hundred and ninety nine"},
    {"1001", "one thousand one"},
    {"0", "zero"},
    {"-1", "minus one"},
    {"-42", "minus forty two"},
    {"-100", "minus one hundred"},
    {"1,000", "one thousand"},
    {"1,000,000", "one million"},
    {"10,500", "ten thousand five hundred"},
    {"999,999", "nine hundred ninety nine thousand nine hundred and ninety nine"},
};
static const int NUM_CARDINAL_TN = sizeof(CARDINAL_TN_CASES) / sizeof(CARDINAL_TN_CASES[0]);

/* --------------------------------------------------------------------------
 * ORDINAL TN test cases  (13 cases)
 * -------------------------------------------------------------------------- */

static const TestCase ORDINAL_TN_CASES[] = {
    {"1st", "first"},
    {"2nd", "second"},
    {"3rd", "third"},
    {"4th", "fourth"},
    {"5th", "fifth"},
    {"10th", "tenth"},
    {"11th", "eleventh"},
    {"12th", "twelfth"},
    {"13th", "thirteenth"},
    {"21st", "twenty first"},
    {"22nd", "twenty second"},
    {"23rd", "twenty third"},
    {"100th", "one hundredth"},
    {"101st", "one hundred first"},
    {"1000th", "one thousandth"},
};
static const int NUM_ORDINAL_TN = sizeof(ORDINAL_TN_CASES) / sizeof(ORDINAL_TN_CASES[0]);

/* --------------------------------------------------------------------------
 * MONEY TN test cases  (13 cases)
 * -------------------------------------------------------------------------- */

static const TestCase MONEY_TN_CASES[] = {
    {"$1", "one dollar"},
    {"$10", "ten dollars"},
    {"$100", "one hundred dollars"},
    {"$1000", "one thousand dollars"},
    {"$1,000,000", "one million dollars"},
    {"$1.50", "one dollar fifty cents"},
    {"$99.99", "ninety nine dollars ninety nine cents"},
    {"$0.99", "ninety nine cents"},
    {"$0.01", "one cent"},
    {"$1.5 million", "one point five million dollars"},
    {"$3.2 billion", "three point two billion dollars"},
    {"\xe2\x82\xac""100", "one hundred euros"},           /* €100 */
    {"\xc2\xa3""50", "fifty pounds"},                     /* £50 */
};
static const int NUM_MONEY_TN = sizeof(MONEY_TN_CASES) / sizeof(MONEY_TN_CASES[0]);

/* --------------------------------------------------------------------------
 * DATE TN test cases  (9 cases)
 * -------------------------------------------------------------------------- */

static const TestCase DATE_TN_CASES[] = {
    {"Jan. 1, 2020", "january first, twenty twenty"},
    {"February 14, 2023", "february fourteenth, twenty twenty three"},
    {"March 3rd, 1999", "march third, nineteen ninety nine"},
    {"Dec 25, 2000", "december twenty fifth, two thousand"},
    {"01/01/2020", "january first twenty twenty"},
    {"12/31/1999", "december thirty first nineteen ninety nine"},
    {"2020-01-15", "january fifteenth twenty twenty"},
    {"1990", "nineteen ninety"},
    {"2000", "two thousand"},
};
static const int NUM_DATE_TN = sizeof(DATE_TN_CASES) / sizeof(DATE_TN_CASES[0]);

/* --------------------------------------------------------------------------
 * TIME TN test cases  (7 cases)
 * -------------------------------------------------------------------------- */

static const TestCase TIME_TN_CASES[] = {
    {"1:00 a.m.", "one AM"},
    {"12:30 p.m.", "twelve thirty PM"},
    {"3:45 PM", "three forty five PM"},
    {"11:59 AM", "eleven fifty nine AM"},
    {"12:00", "twelve o'clock"},
    {"0:00", "zero o'clock"},
    {"23:59", "twenty three fifty nine"},
};
static const int NUM_TIME_TN = sizeof(TIME_TN_CASES) / sizeof(TIME_TN_CASES[0]);

/* --------------------------------------------------------------------------
 * MEASURE TN test cases  (8 cases)
 * -------------------------------------------------------------------------- */

static const TestCase MEASURE_TN_CASES[] = {
    {"100 kg", "one hundred kilograms"},
    {"5.5 km", "five point five kilometers"},
    {"3 ft", "three feet"},
    {"10 lbs", "ten pounds"},
    {"72\xc2\xb0""F", "seventy two degrees Fahrenheit"},  /* 72°F */
    {"100 mph", "one hundred miles per hour"},
    {"50 cm", "fifty centimeters"},
    {"2.5 GHz", "two point five gigahertz"},
};
static const int NUM_MEASURE_TN = sizeof(MEASURE_TN_CASES) / sizeof(MEASURE_TN_CASES[0]);

/* --------------------------------------------------------------------------
 * DECIMAL & FRACTION TN test cases  (10 cases)
 * -------------------------------------------------------------------------- */

static const TestCase DECIMAL_TN_CASES[] = {
    {"0.5", "zero point five"},
    {"3.14", "three point one four"},
    {"100.001", "one hundred point zero zero one"},
    {"-2.5", "minus two point five"},
    {"0.001", "zero point zero zero one"},
    {"1/2", "one half"},
    {"3/4", "three quarters"},
    {"1/3", "one third"},
    {"2/3", "two thirds"},
    {"7/8", "seven eighths"},
};
static const int NUM_DECIMAL_TN = sizeof(DECIMAL_TN_CASES) / sizeof(DECIMAL_TN_CASES[0]);

/* --------------------------------------------------------------------------
 * MIXED SENTENCE TN test cases  (20 cases)
 * -------------------------------------------------------------------------- */

static const TestCase MIXED_TN_CASES[] = {
    {"I have $5 and 3 apples.",
     "I have five dollars and three apples."},
    {"The meeting is at 3:00 p.m. on Jan. 15, 2024.",
     "The meeting is at three PM on january fifteenth, twenty twenty four."},
    {"She ran 5 km in 23 minutes.",
     "She ran five kilometers in twenty three minutes."},
    {"The temperature is 72\xc2\xb0""F today.",
     "The temperature is seventy two degrees Fahrenheit today."},
    {"He weighs 180 lbs and is 6 ft tall.",
     "He weighs one hundred and eighty pounds and is six feet tall."},
    {"Call 1-800-555-0199 for more info.",
     "Call one, eight hundred, five five five, zero one nine nine for more info."},
    {"Visit us at 123 Main St, Apt 4B.",
     "Visit us at one twenty three Main Street, Apt four B."},
    {"The price dropped from $100 to $79.99.",
     "The price dropped from one hundred dollars to seventy nine dollars ninety nine cents."},
    {"Flight AA123 departs at 6:45 AM.",
     "Flight AA one hundred twenty three departs at six forty five AM."},
    {"Born on July 4, 1776.",
     "Born on july fourth, seventeen seventy six."},
    {"Pi is approximately 3.14159.",
     "Pi is approximately three point one four one five nine."},
    {"The recipe calls for 3/4 cup of sugar.",
     "The recipe calls for three quarters cup of sugar."},
    {"Dr. Smith lives at 42 Oak Ave.",
     "doctor Smith lives at forty two Oak Avenue"},
    {"The year 2000 was a leap year.",
     "The year two thousand was a leap year."},
    {"We need 2.5 GHz processor.",
     "We need two point five gigahertz processor."},
    {"The 1st and 2nd floors are closed.",
     "The first and second floors are closed."},
    {"I owe you $3.50.",
     "I owe you three dollars fifty cents."},
    {"The concert is on 12/25/2024.",
     "The concert is on december twenty fifth twenty twenty four."},
    {"He scored 99 out of 100.",
     "He scored ninety nine out of one hundred."},
    {"Add 1,000 to the total.",
     "Add one thousand to the total."},
};
static const int NUM_MIXED_TN = sizeof(MIXED_TN_CASES) / sizeof(MIXED_TN_CASES[0]);

/* --------------------------------------------------------------------------
 * LONG SENTENCE TN test cases  (17 cases)
 * -------------------------------------------------------------------------- */

static const TestCase SENTENCE_TN_CASES[] = {
    {"The invoice total is $2,450.99 and payment is due by March 15, 2025.",
     "The invoice total is two thousand four hundred and fifty dollars ninety nine cents and payment is due by march fifteenth, twenty twenty five."},
    {"Dr. Johnson prescribed 500 mg of ibuprofen to be taken 3 times daily for 7 days.",
     "doctor Johnson prescribed five hundred milligrams of ibuprofen to be taken three times daily for seven days."},
    {"Flight UA2491 departs from gate B12 at 6:45 AM and arrives at 11:30 p.m.",
     "Flight UA two thousand four hundred ninety one departs from gate B twelve at six forty five AM and arrives at eleven thirty PM"},
    {"The property at 1842 N Highland Ave, Los Angeles, CA 90028 sold for $1.2 million.",
     "The property at eighteen forty two North Highland Avenue, Los Angeles, California nine zero zero two eight sold for one point two million dollars."},
    {"Contact support at help@company.com or call +1 800-555-0123 for assistance.",
     "Contact support at help at company dot com or call plus one, eight hundred, five five five, zero one two three for assistance."},
    {"The 3rd quarter earnings report shows revenue of $4.7 billion, up 12% from 2023.",
     "The third quarter earnings report shows revenue of four point seven billion dollars, up twelve percent from twenty twenty three."},
    {"Mix 3/4 cup flour with 1.5 tsp baking powder and 2 eggs at 350\xc2\xb0""F for 25 minutes.",
     "Mix three quarters cup flour with one point five tsp baking powder and two eggs at three hundred and fifty degrees Fahrenheit for twenty five minutes."},
    {"The marathon runner completed the 26.2 mile course in 2:45:33 on April 21, 2024.",
     "The marathon runner completed the twenty six point two mile course in two hours forty five minutes and thirty three seconds on april twenty first, twenty twenty four."},
    {"Server load reached 95% at 3:12 AM on 01/15/2025, affecting 1,200 users.",
     "Server load reached ninety five percent at three twelve AM on january fifteenth twenty twenty five, affecting one thousand two hundred users."},
    {"The Tesla Model 3 accelerates from 0 to 60 mph in 3.1 seconds and costs $42,990.",
     "The Tesla Model three accelerates from zero to sixty miles per hour in three point one seconds and costs forty two thousand nine hundred and ninety dollars."},
    {"Send your RSVP to events@nvidia.com by Dec 31, 2024 for the Jan. 15, 2025 gala.",
     "Send your RSVP to events at NVIDIA dot com by december thirty first, twenty twenty four for the january fifteenth, twenty twenty five gala."},
    {"The 2nd amendment was ratified on December 15, 1791, over 230 years ago.",
     "The second amendment was ratified on december fifteenth, seventeen ninety one, over two hundred and thirty years ago."},
    {"The recipe yields 12 servings of 350 calories each with 8.5 g of protein per serving.",
     "The recipe yields twelve servings of three hundred and fifty calories each with eight point five G of protein per serving."},
    {"On July 20, 1969 at 10:56 p.m. EDT, Neil Armstrong took his 1st steps on the moon.",
     "On july twentieth, nineteen sixty nine at ten fifty six PM EDT, Neil Armstrong took his first steps on the moon."},
    {"The warehouse at 500 Industrial Blvd ships 10,000 packages daily via 25 trucks.",
     "The warehouse at five hundred Industrial Boulevard ships ten thousand packages daily via twenty five trucks."},
    {"The building is 1,454 ft tall with 110 floors and was completed on April 4, 1973.",
     "The building is one thousand four hundred and fifty four feet tall with one hundred and ten floors and was completed on april fourth, nineteen seventy three."},
    {"Train 4521 departs Penn Station at 7:15 AM, stops at 3 stations, and arrives at 9:42 AM.",
     "Train four thousand five hundred and twenty one departs Penn Station at seven fifteen AM, stops at three stations, and arrives at nine forty two AM."},
};
static const int NUM_SENTENCE_TN = sizeof(SENTENCE_TN_CASES) / sizeof(SENTENCE_TN_CASES[0]);

/* ==========================================================================
 * INVERSE TEXT NORMALIZATION (ITN) TEST CASES
 * ========================================================================== */

/* --------------------------------------------------------------------------
 * EMAIL ITN test cases  (7 cases)
 * -------------------------------------------------------------------------- */

static const TestCase EMAIL_ITN_CASES[] = {
    {"a dot b c at g mail dot com", "a.bc@gmail.com"},
    {"c d f at a b c dot e d u", "cdf@abc.edu"},
    {"a b c at a b c dot com", "abc@abc.com"},
    {"a at nvidia dot com", "a@nvidia.com"},
    {"a s d f one two three at a b c dot com", "asdf123@abc.com"},
    {"abc at gmail dot com", "abc@gmail.com"},
    {"n vidia dot com", "nvidia.com"},
};
static const int NUM_EMAIL_ITN = sizeof(EMAIL_ITN_CASES) / sizeof(EMAIL_ITN_CASES[0]);

/* --------------------------------------------------------------------------
 * PHONE ITN test cases  (5 cases)
 * -------------------------------------------------------------------------- */

static const TestCase PHONE_ITN_CASES[] = {
    {"one two three one two three five six seven eight", "123-123-5678"},
    {"plus nine one one two three one two three five six seven eight", "+91 123-123-5678"},
    {"plus forty four one two three one two three five six seven eight", "+44 123-123-5678"},
    {"one two three dot one two three dot o dot four o", "123.123.0.40"},
    {"ssn is seven double nine one two three double one three", "ssn is 799-12-3113"},
};
static const int NUM_PHONE_ITN = sizeof(PHONE_ITN_CASES) / sizeof(PHONE_ITN_CASES[0]);

/* --------------------------------------------------------------------------
 * CARDINAL ITN test cases  (12 cases)
 * -------------------------------------------------------------------------- */

static const TestCase CARDINAL_ITN_CASES[] = {
    {"one", "one"},
    {"twelve", "twelve"},
    {"twenty one", "21"},
    {"ninety nine", "99"},
    {"one hundred", "100"},
    {"one hundred and twenty three", "123"},
    {"one thousand", "1000"},
    {"one million", "1 million"},
    {"ten thousand five hundred", "10500"},
    {"zero", "zero"},
    {"minus forty two", "-42"},
    {"minus one hundred", "-100"},
};
static const int NUM_CARDINAL_ITN = sizeof(CARDINAL_ITN_CASES) / sizeof(CARDINAL_ITN_CASES[0]);

/* --------------------------------------------------------------------------
 * ORDINAL ITN test cases  (6 cases)
 * -------------------------------------------------------------------------- */

static const TestCase ORDINAL_ITN_CASES[] = {
    {"first", "1st"},
    {"second", "2nd"},
    {"third", "3rd"},
    {"tenth", "10th"},
    {"twenty first", "21st"},
    {"one hundredth", "100th"},
};
static const int NUM_ORDINAL_ITN = sizeof(ORDINAL_ITN_CASES) / sizeof(ORDINAL_ITN_CASES[0]);

/* --------------------------------------------------------------------------
 * MONEY ITN test cases  (7 cases)
 * -------------------------------------------------------------------------- */

static const TestCase MONEY_ITN_CASES[] = {
    {"one dollar", "$1"},
    {"ten dollars", "$10"},
    {"one hundred dollars", "$100"},
    {"one dollar fifty cents", "$1.50"},
    {"ninety nine cents", "$0.99"},
    {"five dollars and thirty two cents", "$5.32"},
    {"one million dollars", "$1 million"},
};
static const int NUM_MONEY_ITN = sizeof(MONEY_ITN_CASES) / sizeof(MONEY_ITN_CASES[0]);

/* --------------------------------------------------------------------------
 * DATE ITN test cases  (5 cases)
 * -------------------------------------------------------------------------- */

static const TestCase DATE_ITN_CASES[] = {
    {"january first twenty twenty", "january 1 2020"},
    {"february fourteenth twenty twenty three", "february 14 2023"},
    {"december twenty fifth two thousand", "december 25 2000"},
    {"march third nineteen ninety nine", "march 3 1999"},
    {"july fourth seventeen seventy six", "july 4 1776"},
};
static const int NUM_DATE_ITN = sizeof(DATE_ITN_CASES) / sizeof(DATE_ITN_CASES[0]);

/* --------------------------------------------------------------------------
 * TIME ITN test cases  (4 cases)
 * -------------------------------------------------------------------------- */

static const TestCase TIME_ITN_CASES[] = {
    {"twelve thirty p m", "12:30 p.m."},
    {"three forty five a m", "03:45 a.m."},
    {"one a m", "01:00 a.m."},
    {"eleven fifty nine p m", "11:59 p.m."},
};
static const int NUM_TIME_ITN = sizeof(TIME_ITN_CASES) / sizeof(TIME_ITN_CASES[0]);

/* --------------------------------------------------------------------------
 * MEASURE ITN test cases  (6 cases)
 * -------------------------------------------------------------------------- */

static const TestCase MEASURE_ITN_CASES[] = {
    {"one hundred kilograms", "100 kg"},
    {"five point five kilometers", "5.5 km"},
    {"three feet", "3 ft"},
    {"ten pounds", "ten pounds"},
    {"fifty centimeters", "50 cm"},
    {"one hundred miles per hour", "100 mph"},
};
static const int NUM_MEASURE_ITN = sizeof(MEASURE_ITN_CASES) / sizeof(MEASURE_ITN_CASES[0]);

/* --------------------------------------------------------------------------
 * DECIMAL ITN test cases  (4 cases)
 * -------------------------------------------------------------------------- */

static const TestCase DECIMAL_ITN_CASES[] = {
    {"zero point five", "0.5"},
    {"three point one four", "3.14"},
    {"minus two point five", "-2.5"},
    {"one hundred point zero zero one", "100.001"},
};
static const int NUM_DECIMAL_ITN = sizeof(DECIMAL_ITN_CASES) / sizeof(DECIMAL_ITN_CASES[0]);

/* --------------------------------------------------------------------------
 * FRACTION ITN test cases  (5 cases)
 * -------------------------------------------------------------------------- */

static const TestCase FRACTION_ITN_CASES[] = {
    {"one half", "one half"},
    {"three quarters", "three quarters"},
    {"one third", "one 3rd"},
    {"two thirds", "two thirds"},
    {"seven eighths", "seven eighths"},
};
static const int NUM_FRACTION_ITN = sizeof(FRACTION_ITN_CASES) / sizeof(FRACTION_ITN_CASES[0]);

/* --------------------------------------------------------------------------
 * MIXED SENTENCE ITN test cases  (8 cases)
 * -------------------------------------------------------------------------- */

static const TestCase MIXED_ITN_CASES[] = {
    {"I have five dollars and three apples",
     "I have $5 and three apples"},
    {"She ran five kilometers in twenty three minutes",
     "She ran 5 km in 23 min"},
    {"the first and second floors are closed",
     "the 1st and 2nd floors are closed"},
    {"he scored ninety nine out of one hundred",
     "he scored 99 out of 100"},
    {"add one thousand to the total",
     "add 1000 to the total"},
    {"the year two thousand was a leap year",
     "the year 2000 was a leap year"},
    {"I owe you three dollars fifty cents",
     "I owe you $3.50"},
    {"pi is approximately three point one four",
     "pi is approximately 3.14"},
};
static const int NUM_MIXED_ITN = sizeof(MIXED_ITN_CASES) / sizeof(MIXED_ITN_CASES[0]);

/* --------------------------------------------------------------------------
 * LONG SENTENCE ITN test cases  (19 cases)
 * -------------------------------------------------------------------------- */

static const TestCase SENTENCE_ITN_CASES[] = {
    {"The invoice total is two thousand four hundred and fifty dollars ninety nine cents and payment is due by march fifteenth twenty twenty five.",
     "The invoice total is $2450.99 and payment is due by march 15 2025 ."},
    {"doctor Johnson prescribed five hundred milligrams of ibuprofen to be taken three times daily for seven days.",
     "dr. Johnson prescribed 500 mg of ibuprofen to be taken three times daily for seven days."},
    {"Flight UA twenty four ninety one departs from gate B twelve at six forty five a m and arrives at eleven thirty p m.",
     "Flight UA 2491 departs from gate B twelve at 06:45 a.m. and arrives at 11:30 p.m. ."},
    {"The property at eighteen forty two North Highland Avenue sold for one point two million dollars.",
     "The property at 1842 North Highland Avenue sold for $1.2 million ."},
    {"The third quarter earnings report shows revenue of four point seven billion dollars up twelve percent from twenty twenty three.",
     "The 3rd quarter earnings report shows revenue of $4.7 billion up 12 % from 2023 ."},
    {"Mix three quarters cup flour with one point five teaspoons baking powder and two eggs at three hundred and fifty degrees Fahrenheit for twenty five minutes.",
     "Mix three quarters cup flour with 1.5 teaspoons baking powder and two eggs at 350 \xc2\xb0""F for 25 min ."},
    {"The marathon runner completed the twenty six point two mile course in two hours forty five minutes on april twenty first twenty twenty four.",
     "The marathon runner completed the 26.2 mi course in 2 h 45 min on april 21 2024 ."},
    {"Server load reached ninety five percent at three twelve a m on january fifteenth twenty twenty five affecting one thousand two hundred users.",
     "Server load reached 95 % at 03:12 a.m. on january 15 2025 affecting 1200 users."},
    {"The Tesla Model three accelerates from zero to sixty miles per hour in three point one seconds and costs forty two thousand nine hundred and ninety dollars.",
     "The Tesla Model three accelerates from zero to 60 mph in 3.1 s and costs $42990 ."},
    {"Send your RSVP to events at nvidia dot com by december thirty first twenty twenty four for the january fifteenth twenty twenty five gala.",
     "Send your RSVP to events@nvidia.com by december 31 2024 for the january 15 2025 gala."},
    {"The second amendment was ratified on december fifteenth seventeen ninety one over two hundred and thirty years ago.",
     "The 2nd amendment was ratified on december 15 1791 over 230 years ago."},
    {"On july twentieth nineteen sixty nine at ten fifty six p m Neil Armstrong took his first steps on the moon.",
     "On july 20 1969 at 10:56 p.m. Neil Armstrong took his 1st steps on the moon."},
    {"The warehouse at five hundred Industrial Boulevard ships ten thousand packages daily via twenty five trucks.",
     "The warehouse at 500 Industrial Boulevard ships 10000 packages daily via 25 trucks."},
    {"The building is one thousand four hundred and fifty four feet tall with one hundred and ten floors and was completed on april fourth nineteen seventy three.",
     "The building is 1454 ft tall with 110 floors and was completed on april 4 1973 ."},
    {"Train forty five twenty one departs Penn Station at seven fifteen a m stops at three stations and arrives at nine forty two a m.",
     "Train 4521 departs Penn Station at 07:15 a.m. stops at three stations and arrives at 09:42 a.m. ."},
    {"She earned her PhD at twenty five and published one hundred and twelve papers by age forty two.",
     "She earned her PhD at 25 and published 112 papers by age 42 ."},
    {"The city has a population of eight point four million people spread across three hundred and two square miles.",
     "The city has a population of 8.4 million people spread across 302 sq mi ."},
    {"We drove five hundred and sixty two kilometers from Munich to Paris averaging one hundred and ten kilometers per hour.",
     "We drove 562 km from Munich to Paris averaging 110 km/h ."},
    {"The concert tickets cost seventy five dollars each and the venue holds twelve thousand five hundred people.",
     "The concert tickets cost $75 each and the venue holds 12500 people."},
};
static const int NUM_SENTENCE_ITN = sizeof(SENTENCE_ITN_CASES) / sizeof(SENTENCE_ITN_CASES[0]);

/* --------------------------------------------------------------------------
 * Run a section of tests
 * -------------------------------------------------------------------------- */

typedef struct {
    int passed;
    int failed;
    double total_time;
} SectionResult;

static SectionResult run_section(const char *title,
                                  NemoNormalizer *norm,
                                  const TestCase *cases,
                                  int num_cases) {
    SectionResult res = {0, 0, 0.0};
    char tbuf[32];
    char output[4096];

    printf("\n======================================================================\n");
    printf("  %s\n", title);
    printf("======================================================================\n");

    for (int i = 0; i < num_cases; i++) {
        double t0 = now_sec();
        int rc = nemo_normalize(norm, cases[i].input, output, sizeof(output));
        double elapsed = now_sec() - t0;
        res.total_time += elapsed;

        int ok;
        if (rc == 0) {
            ok = (strcmp(output, cases[i].expected) == 0);
        } else {
            ok = 0;
        }

        fmt_time(elapsed, tbuf, sizeof(tbuf));

        if (ok) {
            res.passed++;
            printf("  [PASS] %8s  \"%s\" -> \"%s\"\n", tbuf, cases[i].input, output);
        } else {
            res.failed++;
            printf("  [FAIL] %8s  \"%s\"\n", tbuf, cases[i].input);
            printf("           expected: \"%s\"\n", cases[i].expected);
            printf("           got:      \"%s\" (rc=%d)\n", output, rc);
        }
    }

    double avg = res.total_time / (num_cases > 0 ? num_cases : 1);
    char ttot[32], tavg[32];
    fmt_time(res.total_time, ttot, sizeof(ttot));
    fmt_time(avg, tavg, sizeof(tavg));

    printf("\n  Results: %d passed, %d failed, %d total\n", res.passed, res.failed, num_cases);
    printf("  Timing:  total=%s  avg=%s\n", ttot, tavg);

    return res;
}

/* --------------------------------------------------------------------------
 * Main
 * -------------------------------------------------------------------------- */

int main(int argc, char *argv[]) {
    /* TN FAR paths */
    const char *tn_classify_far     = "far_export/en_tn_grammars_cased/classify/tokenize_and_classify.far";
    const char *tn_verbalize_far    = "far_export/en_tn_grammars_cased/verbalize/verbalize.far";
    const char *tn_post_process_far = "far_export/en_tn_grammars_cased/verbalize/post_process.far";

    /* ITN FAR paths */
    const char *itn_classify_far    = "far_export/en_itn_grammars_cased/classify/tokenize_and_classify.far";
    const char *itn_verbalize_far   = "far_export/en_itn_grammars_cased/verbalize/verbalize.far";

    (void)argc; (void)argv;

    printf("NeMo Text Processing - C Library Test Suite (TN + ITN)\n");
    printf("======================================================================\n");

    /* ---- Load TN normalizer ---- */
    printf("\n  TN FAR paths:\n");
    printf("    classify:     %s\n", tn_classify_far);
    printf("    verbalize:    %s\n", tn_verbalize_far);
    printf("    post-process: %s\n", tn_post_process_far);

    printf("\nLoading TN normalizer...\n");
    double t0 = now_sec();
    NemoNormalizer *tn = nemo_normalizer_create(tn_classify_far, tn_verbalize_far, tn_post_process_far);
    double tn_init_time = now_sec() - t0;

    if (!tn) {
        fprintf(stderr, "ERROR: Failed to create TN normalizer. Check FAR paths.\n");
        return 1;
    }

    char tinit_tn[32];
    fmt_time(tn_init_time, tinit_tn, sizeof(tinit_tn));
    printf("  TN normalizer ready in %s\n", tinit_tn);

    /* ---- Load ITN normalizer ---- */
    printf("\n  ITN FAR paths:\n");
    printf("    classify:     %s\n", itn_classify_far);
    printf("    verbalize:    %s\n", itn_verbalize_far);

    printf("\nLoading ITN normalizer...\n");
    t0 = now_sec();
    NemoNormalizer *itn = nemo_normalizer_create(itn_classify_far, itn_verbalize_far, NULL);
    double itn_init_time = now_sec() - t0;

    if (!itn) {
        fprintf(stderr, "ERROR: Failed to create ITN normalizer. Check FAR paths.\n");
        nemo_normalizer_destroy(tn);
        return 1;
    }

    char tinit_itn[32];
    fmt_time(itn_init_time, tinit_itn, sizeof(tinit_itn));
    printf("  ITN normalizer ready in %s\n", tinit_itn);

    int total_passed = 0, total_failed = 0;
    double total_time = 0.0;
    SectionResult sr;

    /* ================================================================
     * TEXT NORMALIZATION (TN) tests
     * ================================================================ */

    printf("\n\n##############################################################\n");
    printf("  TEXT NORMALIZATION (TN) - 151 cases\n");
    printf("##############################################################\n");

    sr = run_section("EMAIL - Text Normalization (13 cases)", tn, EMAIL_TN_CASES, NUM_EMAIL_TN);
    total_passed += sr.passed; total_failed += sr.failed; total_time += sr.total_time;

    sr = run_section("PHONE - Text Normalization (12 cases)", tn, PHONE_TN_CASES, NUM_PHONE_TN);
    total_passed += sr.passed; total_failed += sr.failed; total_time += sr.total_time;

    sr = run_section("ADDRESS - Text Normalization (8 cases)", tn, ADDRESS_TN_CASES, NUM_ADDRESS_TN);
    total_passed += sr.passed; total_failed += sr.failed; total_time += sr.total_time;

    sr = run_section("CARDINAL - Text Normalization (19 cases)", tn, CARDINAL_TN_CASES, NUM_CARDINAL_TN);
    total_passed += sr.passed; total_failed += sr.failed; total_time += sr.total_time;

    sr = run_section("ORDINAL - Text Normalization (15 cases)", tn, ORDINAL_TN_CASES, NUM_ORDINAL_TN);
    total_passed += sr.passed; total_failed += sr.failed; total_time += sr.total_time;

    sr = run_section("MONEY - Text Normalization (13 cases)", tn, MONEY_TN_CASES, NUM_MONEY_TN);
    total_passed += sr.passed; total_failed += sr.failed; total_time += sr.total_time;

    sr = run_section("DATE - Text Normalization (9 cases)", tn, DATE_TN_CASES, NUM_DATE_TN);
    total_passed += sr.passed; total_failed += sr.failed; total_time += sr.total_time;

    sr = run_section("TIME - Text Normalization (7 cases)", tn, TIME_TN_CASES, NUM_TIME_TN);
    total_passed += sr.passed; total_failed += sr.failed; total_time += sr.total_time;

    sr = run_section("MEASURE - Text Normalization (8 cases)", tn, MEASURE_TN_CASES, NUM_MEASURE_TN);
    total_passed += sr.passed; total_failed += sr.failed; total_time += sr.total_time;

    sr = run_section("DECIMAL & FRACTION - Text Normalization (10 cases)", tn, DECIMAL_TN_CASES, NUM_DECIMAL_TN);
    total_passed += sr.passed; total_failed += sr.failed; total_time += sr.total_time;

    sr = run_section("MIXED SENTENCES - Text Normalization (20 cases)", tn, MIXED_TN_CASES, NUM_MIXED_TN);
    total_passed += sr.passed; total_failed += sr.failed; total_time += sr.total_time;

    sr = run_section("LONG SENTENCES - Text Normalization (17 cases)", tn, SENTENCE_TN_CASES, NUM_SENTENCE_TN);
    total_passed += sr.passed; total_failed += sr.failed; total_time += sr.total_time;

    /* ================================================================
     * INVERSE TEXT NORMALIZATION (ITN) tests
     * ================================================================ */

    printf("\n\n##############################################################\n");
    printf("  INVERSE TEXT NORMALIZATION (ITN) - 88 cases\n");
    printf("##############################################################\n");

    sr = run_section("EMAIL - Inverse Text Normalization (7 cases)", itn, EMAIL_ITN_CASES, NUM_EMAIL_ITN);
    total_passed += sr.passed; total_failed += sr.failed; total_time += sr.total_time;

    sr = run_section("PHONE - Inverse Text Normalization (5 cases)", itn, PHONE_ITN_CASES, NUM_PHONE_ITN);
    total_passed += sr.passed; total_failed += sr.failed; total_time += sr.total_time;

    sr = run_section("CARDINAL - Inverse Text Normalization (12 cases)", itn, CARDINAL_ITN_CASES, NUM_CARDINAL_ITN);
    total_passed += sr.passed; total_failed += sr.failed; total_time += sr.total_time;

    sr = run_section("ORDINAL - Inverse Text Normalization (6 cases)", itn, ORDINAL_ITN_CASES, NUM_ORDINAL_ITN);
    total_passed += sr.passed; total_failed += sr.failed; total_time += sr.total_time;

    sr = run_section("MONEY - Inverse Text Normalization (7 cases)", itn, MONEY_ITN_CASES, NUM_MONEY_ITN);
    total_passed += sr.passed; total_failed += sr.failed; total_time += sr.total_time;

    sr = run_section("DATE - Inverse Text Normalization (5 cases)", itn, DATE_ITN_CASES, NUM_DATE_ITN);
    total_passed += sr.passed; total_failed += sr.failed; total_time += sr.total_time;

    sr = run_section("TIME - Inverse Text Normalization (4 cases)", itn, TIME_ITN_CASES, NUM_TIME_ITN);
    total_passed += sr.passed; total_failed += sr.failed; total_time += sr.total_time;

    sr = run_section("MEASURE - Inverse Text Normalization (6 cases)", itn, MEASURE_ITN_CASES, NUM_MEASURE_ITN);
    total_passed += sr.passed; total_failed += sr.failed; total_time += sr.total_time;

    sr = run_section("DECIMAL - Inverse Text Normalization (4 cases)", itn, DECIMAL_ITN_CASES, NUM_DECIMAL_ITN);
    total_passed += sr.passed; total_failed += sr.failed; total_time += sr.total_time;

    sr = run_section("FRACTION - Inverse Text Normalization (5 cases)", itn, FRACTION_ITN_CASES, NUM_FRACTION_ITN);
    total_passed += sr.passed; total_failed += sr.failed; total_time += sr.total_time;

    sr = run_section("MIXED SENTENCES - Inverse Text Normalization (8 cases)", itn, MIXED_ITN_CASES, NUM_MIXED_ITN);
    total_passed += sr.passed; total_failed += sr.failed; total_time += sr.total_time;

    sr = run_section("LONG SENTENCES - Inverse Text Normalization (19 cases)", itn, SENTENCE_ITN_CASES, NUM_SENTENCE_ITN);
    total_passed += sr.passed; total_failed += sr.failed; total_time += sr.total_time;

    /* ================================================================
     * Overall Summary
     * ================================================================ */

    int total_cases = total_passed + total_failed;
    char ttot[32], tavg[32];
    fmt_time(total_time, ttot, sizeof(ttot));
    fmt_time(total_cases > 0 ? total_time / total_cases : 0, tavg, sizeof(tavg));

    printf("\n======================================================================\n");
    printf("  OVERALL SUMMARY\n");
    printf("======================================================================\n");
    printf("  TN normalizer init:    %s\n", tinit_tn);
    printf("  ITN normalizer init:   %s\n", tinit_itn);
    printf("  Tests passed:          %d\n", total_passed);
    printf("  Tests failed:          %d\n", total_failed);
    printf("  Total test cases:      %d (151 TN + 88 ITN)\n", total_cases);
    printf("  Total inference time:  %s\n", ttot);
    printf("  Avg per normalization: %s\n", tavg);
    printf("  Throughput:            %.1f normalizations/sec\n",
           total_cases / total_time);
    printf("======================================================================\n");

    nemo_normalizer_destroy(tn);
    nemo_normalizer_destroy(itn);
    return total_failed > 0 ? 1 : 0;
}
