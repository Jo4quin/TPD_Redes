_La idea del archivo es poder ir dejando registro de los problemas que fuimos teniendo. Esto para hacer mas facil esa parte del video. Sino yo me olvido jaja._

# Parte 1

Uno de los problemas encontrados fue a la hora de implementar la funcion de send_and_wait_ack() es que no se reiniciara el el timer si se recibe un numero de secuencia fuera de orden.

# PARTE 2

No fue necesario sincrinizar las VMs ya que estaban sincronizadas al UTC por defecto.

Antes de poder routear las VM tuvimos que instalar el paquete NetTools en cada una de ellas, ya que no venia instalado por defecto.
