# GeoLunar_Chrono_Projection: A Desktop Globe that Marks Cosmic Time

![](./images/1.jpg)


[See the full project on Hackster.io](https://www.hackster.io/xdfu01/geolunar-chrono-projection-c1bdba)

Note on Daylight Saving Time (DST) Handling:
The DST transition logic in the provided code is simplified for readability.
If you require more accurate handling of edge cases (such as exact Sundays for DST changeover), consider using the following more precise implementation instead:
<br>
<br>


void getLocalTime(struct tm* timeinfo, const City* city) {
    // timeinfoから月、日、曜日を取得
    // サマータイムロジックで使うため、月は1-12、曜日は0-6(日曜-土曜)に調整
    
    int currentMonth = timeinfo->tm_mon + 1;
    int currentDay = timeinfo->tm_mday;
    int currentWeekday = timeinfo->tm_wday;

    if (strcmp(city->name, "Tokyo") == 0) {
        // 東京 (JST, UTC+9)
        timeinfo->tm_hour += 9;
    }
    else if (strcmp(city->name, "London") == 0) {
        // --- ロンドンの夏時間(BST)判定ロジック ---
        bool is_bst = false;       

        // 3月: 最終日曜日に開始
        if (currentMonth == 3) {
            if ((currentDay >= 25 && currentWeekday == 0) ||
                (currentDay >= 26 && currentWeekday == 1) ||
                (currentDay >= 27 && currentWeekday == 2) ||
                (currentDay >= 28 && currentWeekday == 3) ||
                (currentDay >= 29 && currentWeekday == 4) ||
                (currentDay >= 30 && currentWeekday == 5) ||
                (currentDay >= 31 && currentWeekday == 6)) {
                is_bst = true;
            }
        }
        // 4月～9月: 期間内
        else if (currentMonth >= 4 && currentMonth <= 9) {
            is_bst = true;
        }
        // 10月: 最終日曜日に終了
        else if (currentMonth == 10) {
            if ((currentDay <= 24 && currentWeekday == 0) ||
                (currentDay <= 25 && currentWeekday == 1) ||
                (currentDay <= 26 && currentWeekday == 2) ||
                (currentDay <= 27 && currentWeekday == 3) ||
                (currentDay <= 28 && currentWeekday == 4) ||
                (currentDay <= 29 && currentWeekday == 5) || // (注) '39'から'29'へ修正
                (currentDay <= 30 && currentWeekday == 6)) {
                is_bst = true;
            }
        }

        // --- オフセット適用 ---
        // 冬時間(GMT)はUTC+0、夏時間(BST)はUTC+1
        if (is_bst) {
            timeinfo->tm_hour += 1;
        }
    }
    else if (strcmp(city->name, "NewYork") == 0) {
        // --- ニューヨークの夏時間(DST)判定ロジック ---
        bool is_ny_dst = false;
        // 3月: 第2日曜日に開始
        if (currentMonth == 3) {
            if ((currentDay >= 8  && currentWeekday == 0) ||
                (currentDay >= 9  && currentWeekday == 1) ||
                (currentDay >= 10 && currentWeekday == 2) ||
                (currentDay >= 11 && currentWeekday == 3) ||
                (currentDay >= 12 && currentWeekday == 4) ||
                (currentDay >= 13 && currentWeekday == 5) ||
                (currentDay >= 14 && currentWeekday == 6)) {
                is_ny_dst = true;
            }
        }
        // 4月～10月: 期間内
        else if (currentMonth >= 4 && currentMonth <= 10) {
            is_ny_dst = true;
        }
        // 11月: 第1日曜日に終了
        else if (currentMonth == 11) {
            if ((currentDay <= 7 && currentWeekday == 0) ||
  　            (currentDay <= 1 && currentWeekday == 1) ||
                (currentDay <= 2 && currentWeekday == 2) ||
                (currentDay <= 3 && currentWeekday == 3) ||
                (currentDay <= 4 && currentWeekday == 4) ||
                (currentDay <= 5 && currentWeekday == 5) ||
                (currentDay <= 6 && currentWeekday == 6)) {
                is_ny_dst = true;
            }
        }

        // --- オフセット適用 ---
        // 冬時間(EST)はUTC-5、夏時間(EDT)はUTC-4
        timeinfo->tm_hour -= 5;
        if (is_ny_dst) {
            timeinfo->tm_hour += 1;
        }
    }
    // 時間の変更をtm構造体に正規化して反映させる
    mktime(timeinfo);
}
