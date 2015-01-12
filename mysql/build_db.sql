drop database if exists tnp;
create database tnp;

GRANT ALL PRIVILEGES ON *.* TO 'root'@'localhost' IDENTIFIED BY 'root' WITH GRANT OPTION;
FLUSH PRIVILEGES;
	
use tnp;

create table sensors(	sensor_id integer NOT NULL AUTO_INCREMENT, ip varchar(15) not null, active boolean not null, start DATETIME not null, end DATETIME, primary key(sensor_id)) engine = InnoDB;
create table rtts(	rtt_id integer NOT NULL AUTO_INCREMENT, sensor_id integer NOT NULL, min double not null, max double not null, avg double not null, dev double not null, time DATETIME not null, primary key(rtt_id), foreign key(sensor_id) references sensors(sensor_id)) engine = InnoDB;

