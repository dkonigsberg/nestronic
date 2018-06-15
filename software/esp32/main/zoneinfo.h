#ifndef ZONEINFO_H
#define ZONEINFO_H

char *zoneinfo_build_region_list();
char *zoneinfo_build_region_zone_list(const char *region);
const char *zoneinfo_get_tz(const char *zone);

#endif /* ZONEINFO_H */
