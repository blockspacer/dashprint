import { Injectable } from '@angular/core';
import { Printer, DiscoveredPrinter } from './Printer';
import { Observable } from 'rxjs/Observable';
import { of } from 'rxjs/observable/of';
import 'rxjs/add/operator/map';
import { HttpClient, HttpHeaders } from '@angular/common/http';

@Injectable()
export class PrintService {

  constructor(private http: HttpClient) { }

  getPrinters() : Observable<Printer[]> {
      return this.http.get<Object>('/api/v1/printers').map(data => {
          let rv: Printer[] = [];

          Object.keys(data).forEach(key => {
              rv.push(new class Printer {
                  id = key;
                  name = data[key].name;
                  defaultPrinter = data[key]['default'];
                  apiKey = data[key].api_key;
                  devicePath = data[key].device_path;
                  baudRate = data[key].baud_rate;
                  stopped = data[key].stopped;
                  connected = data[key].connected;
                  width = data[key].width;
                  height = data[key].height;
                  depth = data[key].depth;
              });
          });

          return rv;
      });
  }

  discoverPrinters() : Observable<DiscoveredPrinter[]> {
      return this.http.post<DiscoveredPrinter[]>('/api/v1/printers/discover', '');
  }

  addPrinter(printer: Printer) : Observable<string> {
      return Observable.create((observer) => {
          this.http.post('/api/v1/printers', {
              name: printer.name,
              default_printer: printer.defaultPrinter,
              device_path: printer.devicePath,
              baud_rate: printer.baudRate,
              stopped: printer.stopped,
              width: printer.width,
              height: printer.height,
              depth: printer.depth
          }, {observe: "response"}).subscribe(resp => {
              // TODO: Error handling
              let newurl = resp.headers.get('Location');
              observer.next(newurl);
              observer.complete();
          });
      });
  }

}
