#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <X11/Xlib.h>

char *tzwrs = "Europe/Warsaw";
static Display *dpy;

char *
smprintf(char *fmt, ...)
{
	va_list fmtargs;
	char *ret;
	int len;

	va_start(fmtargs, fmt);
	len = vsnprintf(NULL, 0, fmt, fmtargs);
	va_end(fmtargs);

	ret = malloc(++len);
	if (ret == NULL) {
		perror("malloc");
		exit(1);
	}

	va_start(fmtargs, fmt);
	vsnprintf(ret, len, fmt, fmtargs);
	va_end(fmtargs);

	return ret;
}

void
settz(char *tzname)
{
	setenv("TZ", tzname, 1);
}

char *
mktimes(char *fmt, char *tzname)
{
	char buf[129];
	time_t tim;
	struct tm *timtm;

	settz(tzname);
	tim = time(NULL);
	timtm = localtime(&tim);
	if (timtm == NULL)
		return smprintf("");

	if (!strftime(buf, sizeof(buf)-1, fmt, timtm)) {
		fprintf(stderr, "strftime == 0\n");
		return smprintf("");
	}

	return smprintf("%s", buf);
}

void
setstatus(char *str)
{
	XStoreName(dpy, DefaultRootWindow(dpy), str);
	XSync(dpy, False);
}

char *
loadavg(void)
{
	double avgs[3];

	if (getloadavg(avgs, 3) < 0)
		return smprintf("");

	return smprintf("%.2f %.2f %.2f", avgs[0], avgs[1], avgs[2]);
}

char *
readfile(char *base, char *file)
{
	char line[513];
	char *path;
	FILE *fd;

	memset(line, 0, sizeof(line));

	path = smprintf("%s/%s", base, file);
	fd = fopen(path, "r");
	free(path);
	if (fd == NULL)
		return NULL;

	if (fgets(line, sizeof(line)-1, fd) == NULL)
		return NULL;
	fclose(fd);

	return smprintf("%s", line);
}

char *
getmem(char *base, char *file)
{
	char *path;
	FILE *fd;
	int memtotal;
	int memfree;
	int buffered;
	int cached;
	int sreclaimable;

	path = smprintf("%s/%s", base, file);
	fd = fopen(path, "r");
	free(path);
	if (fd == NULL)
		return NULL;

	fscanf(fd, "MemTotal: %d kB\n", &memtotal);
	fscanf(fd, "MemFree: %d kB\n", &memfree);
	fscanf(fd, "%*[^\n]\n");
	fscanf(fd, "Buffers: %d kB\n", &buffered);
	fscanf(fd, "Cached: %d kB\n", &cached);
	for (int i = 0; i <= 17; i++)
		fscanf(fd, "%*[^\n]\n");
	fscanf(fd, "SReclaimable: %d kB\n", &sreclaimable);

	return smprintf("%.0fMiB", (float) (memtotal - memfree - cached - buffered - sreclaimable) / 1024);
}

char *
getbattery(char *base)
{
	char *co;
	char status;
	int descap;
	int remcap;

	descap = -1;
	remcap = -1;

	co = readfile(base, "present");
	if (co == NULL)
		return smprintf("");
	if (co[0] != '1') {
		free(co);
		return smprintf("not present");
	}
	free(co);

	co = readfile(base, "charge_full_design");
	if (co == NULL) {
		co = readfile(base, "energy_full_design");
		if (co == NULL)
			return smprintf("");
	}
	sscanf(co, "%d", &descap);
	free(co);

	co = readfile(base, "charge_now");
	if (co == NULL) {
		co = readfile(base, "energy_now");
		if (co == NULL)
			return smprintf("");
	}
	sscanf(co, "%d", &remcap);
	free(co);

	co = readfile(base, "status");
	if (!strncmp(co, "Discharging", 11)) {
		status = '-';
	} else if(!strncmp(co, "Charging", 8)) {
		status = '+';
	} else {
		status = ' ';
	}

	if (remcap < 0 || descap < 0)
		return smprintf("invalid");

	return smprintf("%.0f%%%c", ((float)remcap / (float)descap) * 100, status);
}

char *
gettemperature(char *base, char *sensor)
{
	char *co;

	co = readfile(base, sensor);
	if (co == NULL)
		return smprintf("");
	return smprintf("%02.0f°C", atof(co) / 1000);
}

char *
getcputemperature(char *base, char *sensor1, char *sensor2)
{
	char *co1;
	char *co2;

	co1 = readfile(base, sensor1);
	if (co1 == NULL)
		return smprintf("");
	co2 = readfile(base, sensor2);
	if (co2 == NULL)
		return smprintf("");
	return smprintf("%02.0f°C", (atof(co1) + atof(co2)) / 2000);
}

int
main(void)
{
	char *tcpu;
	char *tgpu;
	char *avgs;
	char *mem;
	char *bat;
	char *tmwrs;
	char *status;

	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "dwmstatus: cannot open display.\n");
		return 1;
	}

	for (;;sleep(20)) {
		tcpu  = getcputemperature("/sys/class/hwmon/hwmon2", "temp2_input", "temp3_input");
		tgpu  = gettemperature("/sys/class/hwmon/hwmon3", "temp2_input");
		avgs  = loadavg();
		mem   = getmem("/proc", "meminfo");
		bat   = getbattery("/sys/class/power_supply/BAT0");
		tmwrs = mktimes("%H:%M", tzwrs);

		status = smprintf(" cpu:%s gpu:%s load:%s mem:%s bat:%s %s",
				tcpu, tgpu, avgs, mem, bat, tmwrs);
		setstatus(status);

		free(tcpu);
		free(tgpu);
		free(avgs);
		free(mem);
		free(bat);
		free(tmwrs);
		free(status);
	}

	XCloseDisplay(dpy);

	return 0;
}
